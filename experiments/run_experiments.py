#!/usr/bin/env python3
# ===========================================================================
#  run_experiments.py -- Experimentacion exhaustiva sobre instancias benchmark
#                         (Inciso 4 del TP: "Experimentacion y discusion")
# ===========================================================================
#
#  QUE HACE ESTE SCRIPT (idea general)
#  ------------------------------------------------------------------------
#  Es un "runner" que llama muchas veces al binario ./gap_simulator (ya
#  compilado a partir de main.cpp), una vez por cada combinacion de:
#     (instancia, metodo, parametros, semilla)
#
#  y junta TODOS los resultados (costo, factibilidad, tiempo, etc.) en un
#  unico archivo CSV: experiments/results.csv. Despues, ese CSV es el input
#  de analyze_results.py, que hace las tablas y graficos para el informe.
#
#  No reimplementa ningun algoritmo: solo invoca al ejecutable como
#  subproceso (como si lo corrieramos a mano desde la terminal muchas veces)
#  y parsea la salida de texto que main.cpp imprime por stdout.
#
#  ESTRUCTURA DE LA EXPERIMENTACION
#  ------------------------------------------------------------------------
#  Para cada instancia de instances/gap/{gap_a,gap_b,gap_e} se corre:
#
#   1) Los 5 metodos DETERMINISTICOS (1 corrida cada uno, no dependen de
#      semilla): greedy, regret, shift, swap, shift_swap.
#      -> Sirven para comparar las heuristicas constructivas entre si y el
#         aporte de cada operador de busqueda local.
#
#   2) ILS (Iterated Local Search) con un GRID de tuning de parametros:
#         iteraciones        in {50, 100, 300}
#         perturb_strength   in {n/100, n/50, n/20}   (n = cantidad de vendedores)
#      y para cada combinacion del grid, 3 semillas distintas (1,2,3), para
#      poder promediar y no quedarnos con un resultado "con suerte".
#      => 3 x 3 x 3 = 27 corridas de ILS por instancia (en instancias chicas).
#
#  Para las instancias MAS GRANDES de gap_e (ver LARGE_INSTANCES) ese grid
#  completo tardaria demasiado (cada corrida de ILS con 300 iteraciones ya
#  tarda > 1 segundo en e40400, y crece con n*m). Por eso, para esas
#  instancias grandes:
#     - se corren igual los 5 metodos deterministicos (son rapidos), y
#     - se corre ILS una sola vez con una config "razonable" fija
#       (200 iteraciones, perturb_strength = n/50) y 2 semillas, en vez del
#       grid completo de 27 corridas.
#  Esa config fija se elige en base a lo que se observa en las instancias
#  chicas (ver el resumen del grid en analyze_results.py).
#
#  COMO SE LE PASAN LOS PARAMETROS AL BINARIO
#  ------------------------------------------------------------------------
#  gap_simulator se invoca asi (ver run_once y main.cpp):
#     ./gap_simulator <instancia> <salida> <metodo> <iteraciones> <seed> <perturb_strength>
#
#  El 6to argumento (perturb_strength) es algo que agregamos a main.cpp
#  especificamente para poder tunear este parametro desde afuera sin
#  recompilar: si se pasa 0, main.cpp usa el default historico (n/50); si se
#  pasa un valor > 0, lo usa directamente. Para los metodos deterministicos
#  (greedy/regret/shift/swap/shift_swap) este parametro no se usa, pero hay
#  que pasarlo igual porque main.cpp espera esa cantidad de argumentos.
#
#  QUE SE GUARDA POR CADA CORRIDA (una fila del CSV)
#  ------------------------------------------------------------------------
#  familia, instancia, n, m, metodo, iteraciones, perturb_strength, seed,
#  costo, sin_asignar, consistente, factible, tiempo_ms, wall_ms
#
#  - costo / sin_asignar / consistente / factible: se parsean de la salida
#    de main.cpp (que ya valida la solucion con recompute_cost_from_scratch
#    y is_feasible -- ver run_and_report en main.cpp).
#  - tiempo_ms: tiempo que main.cpp midio internamente para el algoritmo.
#  - wall_ms: tiempo total del subproceso medido desde python (incluye
#    lectura de instancia + escritura de archivo de salida).
#
#  Uso:
#     make                                          # compilar gap_simulator
#     python3 experiments/run_experiments.py            # corrida completa
#     python3 experiments/run_experiments.py --quick    # subset rapido (smoke test,
#                                                         #  1 instancia por familia)
# ===========================================================================

import csv
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "gap_simulator")
# Archivo de salida "descartable": cada corrida pisa el mismo archivo, porque
# para esta experimentacion solo nos interesan las metricas (costo, tiempo,
# factibilidad), no la asignacion en si. La asignacion final para la
# instancia real se generara por separado (inciso 5).
OUT_SOL = os.path.join(ROOT, "out", "exp_tmp.sol")
RESULTS_CSV = os.path.join(ROOT, "experiments", "results.csv")

GAP_DIRS = ["gap_a", "gap_b", "gap_e"]

# Instancias "grandes" de gap_e que se excluyen de las corridas con muchas
# repeticiones (multiples semillas / grids de parametros), para mantener el
# tiempo total acotado. Se siguen corriendo una vez con los metodos
# deterministicos y con la mejor config de ILS encontrada en las chicas.
LARGE_INSTANCES = {"e201600", "e401600", "e801600", "e60900", "e30900", "e15900"}

# Los 5 metodos "deterministicos": no usan semilla aleatoria, asi que con una
# sola corrida por instancia alcanza (correrlos de nuevo siempre da el mismo
# resultado). Coinciden con los algoritmos que acepta main.cpp via argv[3].
DETERMINISTIC_METHODS = ["greedy", "regret", "shift", "swap", "shift_swap"]

# --- Grid de tuning de ILS ------------------------------------------------
# perturb_strength se expresa como "n/divisor" para que el grid tenga sentido
# en instancias de distinto tamano n: un divisor chico (n/20) => perturbacion
# fuerte (se mueven ~5% de los vendedores por iteracion); un divisor grande
# (n/100) => perturbacion debil (~1%).
ILS_ITERATIONS = [50, 100, 300]
ILS_PERTURB_DIVISORS = [100, 50, 20]  # perturb_strength = max(1, n // divisor)
ILS_SEEDS = [1, 2, 3]

# Expresiones regulares para extraer numeros del texto que imprime main.cpp.
# Ej. de lineas reales:
#   "  asignados          : 100 / 100"
#   "  sin asignar        : 0"
#   "  costo incremental  : 1698.00"
#   "  costo consistente  : SI"
#   "  factible           : SI"
#   "  tiempo algoritmo   : 10.28 ms"
# (run_and_report, usado solo para greedy/regret, imprime "costo" y "tiempo"
#  sin la palabra "incremental"/"algoritmo"; por eso algunos regex tienen
#  partes opcionales / hay un regex alternativo "costo_simple" sin usar
#  porque main siempre termina con el bloque "Resultado" que usa los nombres
#  completos).
RESULT_RE = {
    "asignados": re.compile(r"asignados\s*:\s*(\d+)\s*/\s*(\d+)"),
    "sin_asignar": re.compile(r"sin asignar\s*:\s*(\d+)"),
    "costo_inc": re.compile(r"costo incremental\s*:\s*([0-9.eE+-]+)"),
    "costo_simple": re.compile(r"costo\s+:\s*([0-9.eE+-]+)"),
    "consistente": re.compile(r"consistente\s*:\s*(SI|NO)"),
    "factible": re.compile(r"factible\s*:\s*(SI|NO)"),
    "tiempo": re.compile(r"tiempo (?:algoritmo)?\s*:\s*([0-9.]+) ms"),
}


def list_instances(quick=False):
    """Devuelve la lista de instancias a correr como tuplas (familia, nombre, path).

    Recorre instances/gap/gap_a, gap_b y gap_e y lista TODOS los archivos
    que encuentra ahi (son las 27 instancias benchmark provistas por la
    catedra). Con --quick se devuelve solo 1 instancia chica por familia,
    para verificar rapido que el pipeline completo (compilar, correr,
    parsear, escribir CSV) funciona antes de lanzar la corrida larga.
    """
    instances = []
    for d in GAP_DIRS:
        full_dir = os.path.join(ROOT, "instances", "gap", d)
        for name in sorted(os.listdir(full_dir)):
            path = os.path.join(full_dir, name)
            if os.path.isfile(path):
                instances.append((d, name, path))
    if quick:
        # un subset chico para smoke test
        instances = [t for t in instances if t[1] in
                      ("a05100", "b05100", "e05100")]
    return instances


def parse_output(text):
    """Extrae las metricas relevantes de la salida de texto de gap_simulator.

    main.cpp imprime, al final de cada corrida, un bloque "---- Resultado: ... ----"
    con varias lineas "campo : valor". Esta funcion busca cada campo con su
    regex correspondiente y devuelve un dict solo con lo que encontro (si
    algun campo no aparece, simplemente no esta en el dict resultante).
    """
    res = {}
    m = RESULT_RE["asignados"].search(text)
    if m:
        res["asignados"] = int(m.group(1))
        res["n"] = int(m.group(2))
    m = RESULT_RE["sin_asignar"].search(text)
    if m:
        res["sin_asignar"] = int(m.group(1))
    m = RESULT_RE["costo_inc"].search(text)
    if m:
        res["costo"] = float(m.group(1))
    m = RESULT_RE["consistente"].search(text)
    if m:
        res["consistente"] = m.group(1)
    m = RESULT_RE["factible"].search(text)
    if m:
        res["factible"] = m.group(1)
    m = RESULT_RE["tiempo"].search(text)
    if m:
        res["tiempo_ms"] = float(m.group(1))
    return res


def run_once(instance_path, method, iterations=100, seed=1234567, perturb=0):
    """Corre UNA vez gap_simulator y devuelve sus metricas como dict.

    Equivale a ejecutar a mano:
        ./gap_simulator <instance_path> out/exp_tmp.sol <method> <iterations> <seed> <perturb>

    - Para metodos deterministicos (greedy, regret, shift, swap, shift_swap),
      `iterations`, `seed` y `perturb` se ignoran dentro de main.cpp, pero se
      pasan igual porque el programa espera esa cantidad de argumentos.
    - `perturb=0` le indica a main.cpp que use su default (n/50); un valor
      > 0 fuerza ese perturb_strength exacto (solo tiene efecto con ils).
    - Medimos tambien `wall_ms`: tiempo de pared del subproceso completo
      (lectura de instancia + algoritmo + escritura de salida), util para
      comparar contra `tiempo_ms` (que main.cpp mide SOLO para el algoritmo).
    - Si el proceso devuelve un codigo de error inesperado (ni 0=OK ni
      2=resultado invalido detectado por main.cpp), se imprime un aviso por
      stderr para no pasarlo por alto silenciosamente.
    """
    cmd = [BIN, instance_path, OUT_SOL, method, str(iterations), str(seed), str(perturb)]
    t0 = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    wall_ms = (time.perf_counter() - t0) * 1000.0
    if proc.returncode not in (0, 2):
        print(f"  !! ERROR ({proc.returncode}) en {cmd}\n{proc.stderr}", file=sys.stderr)
    res = parse_output(proc.stdout)
    res["wall_ms"] = wall_ms
    return res


def main():
    # --quick corre un subset chico (1 instancia por familia, sin grid
    # completo de ILS) solo para verificar que todo el pipeline funciona.
    quick = "--quick" in sys.argv

    # Si todavia no existe el binario, lo compilamos (equivalente a `make`).
    if not os.path.exists(BIN):
        print("Compilando gap_simulator (make)...")
        subprocess.run(["make"], cwd=ROOT, check=True)

    os.makedirs(os.path.dirname(OUT_SOL), exist_ok=True)
    os.makedirs(os.path.dirname(RESULTS_CSV), exist_ok=True)

    instances = list_instances(quick=quick)
    print(f"Total instancias: {len(instances)}")

    # Columnas del CSV de salida. Cada corrida de gap_simulator genera UNA
    # fila con esta forma. Para los metodos deterministicos, las columnas
    # iteraciones/perturb_strength/seed quedan vacias (None) porque no
    # aplican.
    fieldnames = ["familia", "instancia", "n", "m", "metodo", "iteraciones",
                   "perturb_strength", "seed", "costo", "sin_asignar",
                   "consistente", "factible", "tiempo_ms", "wall_ms"]

    rows = []  # se va a volcar entero a RESULTS_CSV al final

    # Recorremos cada instancia benchmark (27 en total: gap_a + gap_b + gap_e).
    for (fam, name, path) in instances:
        print(f"\n=== {fam}/{name} ===")

        # -------------------------------------------------------------
        # PASO 1: metodos deterministicos.
        # Cada uno se corre UNA sola vez (no dependen de semilla): greedy
        # y regret son las heuristicas constructivas (inciso 1); shift,
        # swap y shift_swap son "mejor constructiva + busqueda local"
        # (inciso 2). Esto permite comparar:
        #   - greedy vs regret      (constructivas entre si)
        #   - shift vs swap vs shift_swap  (operadores de BL entre si)
        #   - constructiva sola vs constructiva + BL (aporte de la BL)
        # -------------------------------------------------------------
        for method in DETERMINISTIC_METHODS:
            res = run_once(path, method)
            print(f"  {method:12s} costo={res.get('costo'):>12.2f} "
                  f"sin_asignar={res.get('sin_asignar')} "
                  f"tiempo={res.get('tiempo_ms'):.2f} ms")
            rows.append({
                "familia": fam, "instancia": name,
                "n": res.get("n"), "m": None,
                "metodo": method, "iteraciones": None,
                "perturb_strength": None, "seed": None,
                "costo": res.get("costo"), "sin_asignar": res.get("sin_asignar"),
                "consistente": res.get("consistente"), "factible": res.get("factible"),
                "tiempo_ms": res.get("tiempo_ms"), "wall_ms": res.get("wall_ms"),
            })

        # -------------------------------------------------------------
        # PASO 2 (caso instancias grandes): salteamos el grid completo.
        #
        # El grid de ILS son 27 corridas (3 iters x 3 perturb x 3 seeds), y
        # cada corrida de ILS con muchas iteraciones escala con n*m por la
        # busqueda local shift_swap dentro de cada iteracion. Para las
        # instancias mas grandes de gap_e (e15900 en adelante, ver
        # LARGE_INSTANCES) eso seria prohibitivo en tiempo.
        #
        # En su lugar, corremos ILS UNA sola config "razonable" (200
        # iteraciones, perturb_strength = n/50) con 2 semillas. Esa config
        # se eligio mirando los resultados del grid en las instancias
        # chicas/medianas (ver heatmap en analyze_results.py: n/50 con 200
        # iteraciones empata con la mejor config encontrada, 0% de gap).
        #
        # `continue` al final salta directo a la siguiente instancia, sin
        # entrar al grid completo del paso 3.
        # -------------------------------------------------------------
        if name in LARGE_INSTANCES and not quick:
            n = rows[-1]["n"] or 0
            for seed in ILS_SEEDS[:2]:
                res = run_once(path, "ils", iterations=200, seed=seed,
                                perturb=max(1, n // 50))
                print(f"  ils(200,n/50,seed={seed}) costo={res.get('costo'):>12.2f} "
                      f"tiempo={res.get('tiempo_ms'):.2f} ms")
                rows.append({
                    "familia": fam, "instancia": name,
                    "n": res.get("n"), "m": None,
                    "metodo": "ils", "iteraciones": 200,
                    "perturb_strength": max(1, n // 50), "seed": seed,
                    "costo": res.get("costo"), "sin_asignar": res.get("sin_asignar"),
                    "consistente": res.get("consistente"), "factible": res.get("factible"),
                    "tiempo_ms": res.get("tiempo_ms"), "wall_ms": res.get("wall_ms"),
                })
            continue

        # -------------------------------------------------------------
        # PASO 3: grid completo de tuning de ILS (instancias chicas/medianas).
        #
        # Recorremos las 3x3 combinaciones de (iteraciones, perturb_strength)
        # y para cada una corremos 3 semillas distintas. perturb_strength se
        # calcula a partir de n (cantidad de vendedores de ESTA instancia) y
        # del divisor del grid: n/100 (perturbacion debil), n/50 (media),
        # n/20 (fuerte). `max(1, ...)` evita perturb_strength=0 en instancias
        # muy chicas (ej. n=100 -> n/100=1, ok; pero si n fuera < divisor
        # daria 0, que no tendria efecto).
        #
        # Resultado: para cada instancia chica/mediana quedan 27 filas de
        # ILS en el CSV (3 iters x 3 perturb x 3 seeds), que despues
        # analyze_results.py promedia sobre las 3 semillas para encontrar la
        # mejor combinacion de parametros.
        # -------------------------------------------------------------
        n_val = rows[-1]["n"] or 100
        for iters in ILS_ITERATIONS:
            for div in ILS_PERTURB_DIVISORS:
                perturb = max(1, n_val // div)
                for seed in ILS_SEEDS:
                    res = run_once(path, "ils", iterations=iters, seed=seed, perturb=perturb)
                    rows.append({
                        "familia": fam, "instancia": name,
                        "n": res.get("n"), "m": None,
                        "metodo": "ils", "iteraciones": iters,
                        "perturb_strength": perturb, "seed": seed,
                        "costo": res.get("costo"), "sin_asignar": res.get("sin_asignar"),
                        "consistente": res.get("consistente"), "factible": res.get("factible"),
                        "tiempo_ms": res.get("tiempo_ms"), "wall_ms": res.get("wall_ms"),
                    })
                # Log corto: muestra el costo de las 3 semillas que se acaban
                # de agregar (las ultimas 3 filas de `rows`), para seguir el
                # progreso sin tener que abrir el CSV.
                print(f"  ils(iters={iters:4d}, perturb=n/{div:<3d}={perturb:4d}) "
                      f"costos seeds={[round(rows[-3+k]['costo'],1) for k in range(3)]}")

    # Volcado final: una fila por corrida, todas las instancias y metodos.
    with open(RESULTS_CSV, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"\nResultados escritos en {RESULTS_CSV} ({len(rows)} filas)")


if __name__ == "__main__":
    main()

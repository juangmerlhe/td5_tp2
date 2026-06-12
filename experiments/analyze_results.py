#!/usr/bin/env python3
# ===========================================================================
#  analyze_results.py -- Resumen de experiments/results.csv
#                         (Inciso 4 del TP: "Experimentacion y discusion")
# ===========================================================================
#
#  QUE HACE ESTE SCRIPT (idea general)
#  ------------------------------------------------------------------------
#  run_experiments.py genera una fila por cada corrida de gap_simulator
#  (~700 filas en total: deterministicos + grid de ILS x semillas, para las
#  27 instancias benchmark). Ese CSV "crudo" es dificil de leer a mano.
#
#  Este script LEE ese CSV y produce 3 tablas resumen + 3 graficos, que son
#  los que se pegan directamente en el informe del inciso 4. No corre nada
#  de gap_simulator; solo agrega/promedia lo que ya esta en results.csv.
#
#  LAS 3 TABLAS (y su pregunta de investigacion)
#  ------------------------------------------------------------------------
#   1) summary_deterministic.csv
#      Pregunta: "¿que tan buena es cada heuristica/operador SIN aleatoriedad?"
#      Una fila por instancia, con el costo (y tiempo) de greedy, regret,
#      shift, swap y shift_swap. Permite comparar:
#        - regret vs greedy        (constructivas)
#        - shift / swap / shift_swap vs constructiva sola (aporte de la BL)
#        - shift vs swap vs shift_swap entre si (que operador aporta mas)
#
#   2) summary_ils_best.csv
#      Pregunta: "para ESTA instancia, ¿cual es la mejor config de ILS, y
#      cuanto mejora respecto de quedarse con shift_swap (sin metaheuristica)?"
#      Para cada instancia: se promedia el costo de ILS sobre las 3 semillas
#      para cada combinacion (iteraciones, perturb_strength) del grid, se
#      toma la combinacion con menor costo promedio ("best_cfg"), y se la
#      compara contra el costo de shift_swap (mejora_pct_vs_shift_swap).
#
#   3) summary_ils_grid.csv
#      Pregunta: "en GENERAL (no para una instancia particular), ¿que
#      combinacion de parametros de ILS conviene usar por default?"
#      Para cada combinacion (iteraciones, perturb_strength) del grid, se
#      calcula que tan lejos quedo (en %) del mejor resultado de CADA
#      instancia, y se promedia esa distancia ("gap %") entre instancias.
#      Una combinacion con gap%=0 significa "fue la mejor (o empato con la
#      mejor) en todas las instancias donde se probo". Esta tabla es la base
#      para elegir los parametros que se van a usar despues en la instancia
#      real (inciso 5).
#
#  NOTA SOBRE "perturb_divisor":
#  run_experiments.py corrio perturb_strength = n // divisor, con divisor en
#  {100, 50, 20} (n = cantidad de vendedores de CADA instancia). Como n varia
#  entre instancias, perturb_strength en valor absoluto no es comparable
#  (perturb_strength=2 es "fuerte" para n=100 pero "debil" para n=1600).
#  Por eso, en este script reconstruimos el divisor aproximado (n/perturb)
#  para poder agrupar/promediar configs equivalentes entre instancias de
#  distinto tamano (funcion to_divisor).
#
#  LOS 3 GRAFICOS (matplotlib, en experiments/plots/)
#  ------------------------------------------------------------------------
#   - deterministic_gap.png : version grafica de la tabla 1. Para cada
#     instancia y cada metodo determinista, gap %% = cuanto mas caro es ese
#     metodo respecto al mejor de los 5 en esa instancia (0% = es el mejor).
#
#   - ils_grid_heatmap.png  : version grafica de la tabla 3. Filas =
#     iteraciones, columnas = perturb_strength (como n/divisor), color/valor
#     = gap %% promedio (mas oscuro/alto = peor). Permite ver de un vistazo
#     que zona del grid conviene.
#
#   - ils_vs_shift_swap.png : version grafica de la columna
#     "mejora_pct_vs_shift_swap" de la tabla 2: cuanto gana la mejor config
#     de ILS sobre shift_swap, por instancia.
#
#  Requisitos: pip install matplotlib (y numpy<2 si da error de ABI).
#
#  Uso: python3 experiments/analyze_results.py
#       (requiere que antes se haya corrido run_experiments.py y exista
#        experiments/results.csv)
# ===========================================================================

import csv
import os
import statistics as st
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS_CSV = os.path.join(ROOT, "experiments", "results.csv")
EXP_DIR = os.path.join(ROOT, "experiments")
PLOTS_DIR = os.path.join(EXP_DIR, "plots")


def load_rows():
    """Lee experiments/results.csv entero como lista de dicts (1 dict por fila/corrida)."""
    with open(RESULTS_CSV) as f:
        return list(csv.DictReader(f))


def to_float(x):
    """Convierte un string del CSV a float, o None si esta vacio/no es numero.

    Todas las columnas del CSV se leen como string (csv.DictReader). Las
    columnas que no aplican (ej. 'seed' en una fila de metodo determinista)
    quedan como "" y to_float las convierte en None en vez de tirar error.
    """
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def main():
    rows = load_rows()

    # Lista de todas las (familia, instancia) presentes en el CSV, ordenada
    # alfabeticamente. Es la "columna" sobre la que iteran las 3 tablas.
    instances = sorted(set((r["familia"], r["instancia"]) for r in rows))

    # =======================================================================
    # TABLA 1: metodos deterministicos
    # =======================================================================
    # Para cada instancia, buscamos en `rows` las filas de greedy/regret/
    # shift/swap/shift_swap (seed == "" las identifica como deterministicas,
    # ver run_experiments.py) y armamos un dict "ancho":
    #   {familia, instancia, costo_greedy, tiempo_greedy_ms, costo_regret, ...}
    # Esto es un "pivot" de formato largo (1 fila por corrida) a formato
    # ancho (1 fila por instancia, 1 columna por metodo) para que sea facil
    # de leer/graficar.
    det_methods = ["greedy", "regret", "shift", "swap", "shift_swap"]
    det_rows = []
    for fam, name in instances:
        entry = {"familia": fam, "instancia": name}
        for r in rows:
            if r["instancia"] == name and r["metodo"] in det_methods and r["seed"] == "":
                entry[f"costo_{r['metodo']}"] = to_float(r["costo"])
                entry[f"tiempo_{r['metodo']}_ms"] = to_float(r["tiempo_ms"])
        det_rows.append(entry)

    with open(os.path.join(EXP_DIR, "summary_deterministic.csv"), "w", newline="") as f:
        fieldnames = ["familia", "instancia"] + \
            [f"costo_{m}" for m in det_methods] + \
            [f"tiempo_{m}_ms" for m in det_methods]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(det_rows)

    print("=" * 70)
    print("1) METODOS DETERMINISTICOS (costo)")
    print("=" * 70)
    print(f"{'instancia':12s} " + " ".join(f"{m:>12s}" for m in det_methods))
    for e in det_rows:
        vals = [e.get(f"costo_{m}") for m in det_methods]
        print(f"{e['instancia']:12s} " + " ".join(
            f"{v:12.1f}" if v is not None else f"{'-':>12s}" for v in vals))

    # =======================================================================
    # TABLA 2: mejor config de ILS por instancia, vs shift_swap
    # =======================================================================
    # Filtramos solo las filas de ILS que tienen semilla (seed != ""), o sea
    # todas las corridas del grid de tuning (y las de las instancias grandes).
    ils_rows = [r for r in rows if r["metodo"] == "ils" and r["seed"] != ""]

    # perturb_strength se guardo en el CSV como un numero absoluto (ej. 5),
    # pero ese numero solo tiene sentido en relacion a n de esa instancia.
    # Para poder comparar entre instancias, reconstruimos el "divisor"
    # aproximado d tal que perturb_strength ~= n/d, y lo redondeamos al
    # divisor mas cercano de los que efectivamente uso run_experiments.py
    # (100, 50, 20). Ej: n=200, perturb_strength=10 -> n/perturb=20 -> div=20.
    DIVISORS = [100, 50, 20]

    def to_divisor(n, perturb):
        n = to_float(n) or 0
        perturb = to_float(perturb) or 1
        raw = n / perturb if perturb else 0
        return min(DIVISORS, key=lambda d: abs(d - raw))

    best_rows = []
    # grid_costs acumula, para cada combinacion (iteraciones, divisor), una
    # lista con el "gap% respecto al mejor de ESA instancia" -- una entrada
    # por instancia donde se probo esa combinacion. Se usa despues en la
    # Tabla 3 para promediar entre instancias.
    grid_costs = defaultdict(list)

    for fam, name in instances:
        sub = [r for r in ils_rows if r["instancia"] == name]
        if not sub:
            continue

        # Agrupamos las corridas de ILS de esta instancia por config
        # (iteraciones, divisor) y promediamos el costo sobre las semillas.
        # cfg_vals[(iters, div)] = [costo_seed1, costo_seed2, costo_seed3]
        cfg_vals = defaultdict(list)
        for r in sub:
            key = (r["iteraciones"], to_divisor(r["n"], r["perturb_strength"]))
            cfg_vals[key].append(to_float(r["costo"]))
        cfg_avg = {k: st.mean(v) for k, v in cfg_vals.items()}
        if not cfg_avg:
            continue

        # La "mejor config" de esta instancia es la de menor costo promedio.
        best_cfg = min(cfg_avg, key=cfg_avg.get)
        best_cost = cfg_avg[best_cfg]

        # Comparamos esa mejor config contra el costo de shift_swap (el
        # mejor metodo determinista, ver tabla 1) para esta misma instancia,
        # para cuantificar cuanto aporta la metaheuristica por sobre la
        # busqueda local sola.
        ss_cost = next((to_float(r["costo"]) for r in rows
                         if r["instancia"] == name and r["metodo"] == "shift_swap"), None)
        improvement = None
        if ss_cost and ss_cost != 0:
            improvement = 100.0 * (ss_cost - best_cost) / ss_cost

        best_rows.append({
            "familia": fam, "instancia": name,
            "best_iters": best_cfg[0], "best_perturb_divisor": best_cfg[1],
            "best_avg_cost": round(best_cost, 2),
            "shift_swap_cost": ss_cost,
            "mejora_pct_vs_shift_swap": round(improvement, 2) if improvement is not None else None,
        })

        # Para la tabla 3: por cada config probada en esta instancia,
        # registramos cuanto % mas cara fue que la mejor config de ESTA
        # instancia (0% = era la mejor, o empataba con ella).
        for (it, div), avg in cfg_avg.items():
            gap_pct = 100.0 * (avg - best_cost) / best_cost if best_cost else 0.0
            grid_costs[(it, div)].append(gap_pct)

    with open(os.path.join(EXP_DIR, "summary_ils_best.csv"), "w", newline="") as f:
        fieldnames = ["familia", "instancia", "best_iters", "best_perturb_divisor",
                      "best_avg_cost", "shift_swap_cost", "mejora_pct_vs_shift_swap"]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(best_rows)

    print()
    print("=" * 70)
    print("2) MEJOR CONFIG DE ILS POR INSTANCIA (vs shift_swap)")
    print("=" * 70)
    print(f"{'instancia':12s} {'iters':>6s} {'n/div':>8s} {'costo_ils':>12s} "
          f"{'costo_ss':>12s} {'mejora%':>8s}")
    for e in best_rows:
        print(f"{e['instancia']:12s} {e['best_iters']:>6} {e['best_perturb_divisor']:>8} "
              f"{e['best_avg_cost']:>12.1f} "
              f"{e['shift_swap_cost'] if e['shift_swap_cost'] is not None else float('nan'):>12.1f} "
              f"{e['mejora_pct_vs_shift_swap'] if e['mejora_pct_vs_shift_swap'] is not None else float('nan'):>7.2f}%")

    # =======================================================================
    # TABLA 3: resumen global del grid de ILS
    # =======================================================================
    # Para cada combinacion (iteraciones, divisor) del grid, promediamos su
    # lista de gap% (uno por instancia donde se probo, calculado arriba) en
    # un solo numero "gap_pct_promedio". Cuanto MAS BAJO, mejor es esa
    # combinacion EN PROMEDIO sobre todas las instancias (0% = empato con la
    # mejor config en todas las instancias donde se probo). n_instancias
    # indica sobre cuantas instancias se promedio (las instancias grandes
    # solo aportan a la combinacion (200, n/50), por como esta armado
    # run_experiments.py).
    grid_rows = []
    for (it, div), gaps in sorted(grid_costs.items(), key=lambda kv: (int(kv[0][0]), int(kv[0][1]))):
        grid_rows.append({
            "iteraciones": it, "perturb_divisor": div,
            "gap_pct_promedio": round(st.mean(gaps), 3),
            "n_instancias": len(gaps),
        })

    with open(os.path.join(EXP_DIR, "summary_ils_grid.csv"), "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["iteraciones", "perturb_divisor",
                                                  "gap_pct_promedio", "n_instancias"])
        writer.writeheader()
        writer.writerows(grid_rows)

    print()
    print("=" * 70)
    print("3) RESUMEN GLOBAL DEL GRID DE ILS (gap %% promedio vs mejor por instancia)")
    print("=" * 70)
    print(f"{'iters':>6s} {'n/divisor':>10s} {'gap%% promedio':>15s} {'n_inst':>7s}")
    for g in sorted(grid_rows, key=lambda r: r["gap_pct_promedio"]):
        print(f"{g['iteraciones']:>6} {g['perturb_divisor']:>10} "
              f"{g['gap_pct_promedio']:>14.3f}% {g['n_instancias']:>7}")

    print(f"\nArchivos generados en {EXP_DIR}: summary_deterministic.csv, "
          f"summary_ils_best.csv, summary_ils_grid.csv")

    # --- Graficos ----------------------------------------------------------
    os.makedirs(PLOTS_DIR, exist_ok=True)
    plot_deterministic_gap(det_rows, det_methods)
    plot_ils_grid_heatmap(grid_rows)
    plot_ils_vs_shift_swap(best_rows)
    print(f"Graficos generados en {PLOTS_DIR}: deterministic_gap.png, "
          f"ils_grid_heatmap.png, ils_vs_shift_swap.png")


def plot_deterministic_gap(det_rows, det_methods):
    """Para cada instancia, gap %% de cada metodo determinista respecto al
    mejor (menor costo) entre los 5. Barras agrupadas por instancia."""
    names = [e["instancia"] for e in det_rows]
    gaps = {m: [] for m in det_methods}
    for e in det_rows:
        vals = {m: e.get(f"costo_{m}") for m in det_methods}
        best = min(v for v in vals.values() if v is not None)
        for m in det_methods:
            v = vals.get(m)
            gaps[m].append(100.0 * (v - best) / best if (v is not None and best) else 0.0)

    x = range(len(names))
    n_methods = len(det_methods)
    width = 0.8 / n_methods

    fig, ax = plt.subplots(figsize=(14, 5))
    for k, m in enumerate(det_methods):
        offset = (k - (n_methods - 1) / 2) * width
        ax.bar([xi + offset for xi in x], gaps[m], width=width, label=m)

    ax.set_xticks(list(x))
    ax.set_xticklabels(names, rotation=60, ha="right")
    ax.set_ylabel("Gap %% vs mejor metodo determinista")
    ax.set_title("Comparacion de constructivas y busquedas locales por instancia")
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(PLOTS_DIR, "deterministic_gap.png"), dpi=150)
    plt.close(fig)


def plot_ils_grid_heatmap(grid_rows):
    """Heatmap iteraciones x perturb_divisor con el gap %% promedio del grid."""
    iters_vals = sorted(set(int(g["iteraciones"]) for g in grid_rows))
    div_vals = sorted(set(int(g["perturb_divisor"]) for g in grid_rows))

    matrix = [[float("nan")] * len(div_vals) for _ in iters_vals]
    for g in grid_rows:
        i = iters_vals.index(int(g["iteraciones"]))
        j = div_vals.index(int(g["perturb_divisor"]))
        matrix[i][j] = g["gap_pct_promedio"]

    fig, ax = plt.subplots(figsize=(7, 5))
    im = ax.imshow(matrix, cmap="viridis_r", aspect="auto")
    ax.set_xticks(range(len(div_vals)))
    ax.set_xticklabels([f"n/{d}" for d in div_vals])
    ax.set_yticks(range(len(iters_vals)))
    ax.set_yticklabels(iters_vals)
    ax.set_xlabel("perturb_strength")
    ax.set_ylabel("iteraciones")
    ax.set_title("Gap %% promedio de ILS vs mejor config por instancia")

    for i in range(len(iters_vals)):
        for j in range(len(div_vals)):
            val = matrix[i][j]
            if val == val:  # not NaN
                ax.text(j, i, f"{val:.1f}", ha="center", va="center",
                        color="white" if val > 100 else "black", fontsize=8)

    fig.colorbar(im, ax=ax, label="gap %% (menor es mejor)")
    fig.tight_layout()
    fig.savefig(os.path.join(PLOTS_DIR, "ils_grid_heatmap.png"), dpi=150)
    plt.close(fig)


def plot_ils_vs_shift_swap(best_rows):
    """Mejora %% de ILS (mejor config encontrada) vs shift_swap, por instancia."""
    names = [e["instancia"] for e in best_rows]
    mejoras = [e["mejora_pct_vs_shift_swap"] or 0.0 for e in best_rows]

    fig, ax = plt.subplots(figsize=(14, 5))
    colors = ["#2a9d8f" if m > 0 else "#e76f51" for m in mejoras]
    ax.bar(range(len(names)), mejoras, color=colors)
    ax.set_xticks(range(len(names)))
    ax.set_xticklabels(names, rotation=60, ha="right")
    ax.set_ylabel("Mejora %% de ILS vs shift_swap")
    ax.set_title("Ganancia de ILS (mejor config) respecto a shift_swap")
    ax.axhline(0, color="black", linewidth=0.8)
    fig.tight_layout()
    fig.savefig(os.path.join(PLOTS_DIR, "ils_vs_shift_swap.png"), dpi=150)
    plt.close(fig)


if __name__ == "__main__":
    main()

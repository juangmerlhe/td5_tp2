#!/usr/bin/env bash
# ===========================================================================
#  run_experiments.sh  --  Sistematizacion de la experimentacion (inciso 4)
# ===========================================================================
#
#  IDEA GENERAL:
#
#  Estamos comparando distintas configuraciones de los metodos implementados
#  (constructivas, busqueda local, ILS) para entender como impactan sobre la
#  calidad de la solucion obtenida y el tiempo de ejecucion.
#
#  El objetivo no es solamente obtener una buena solucion, sino tambien
#  estudiar el comportamiento del algoritmo: rendimientos decrecientes al
#  aumentar iteraciones, efecto de la intensidad de perturbacion, valor (o
#  no) de aceptar soluciones peores, y varianza entre semillas.
#
#  El experimento tiene dos fases:
#
#    FASE 1 - comparacion de metodos. Corre los metodos deterministicos
#    (greedy, regret, shift, swap, shift_swap) una vez por instancia, y el
#    ILS con configuracion default sobre varias semillas. Responde "cuanto
#    aporta cada capa" (constructiva -> +BL -> +ILS).
#
#    FASE 2 - tuning del ILS. Grilla completa sobre
#       iteraciones x strength x criterio_de_aceptacion x semilla.
#    Responde "que configuracion conviene" y alimenta el inciso 5 (la
#    configuracion ganadora es la que corremos sobre la instancia real).
#
#  La salida es UN unico CSV (results/experiments.csv) con una fila por
#  corrida. Cada fila incluye todos los factores y todas las metricas, de
#  modo que las tablas y graficos del informe se derivan de este archivo
#  sin re-ejecutar nada.
#
#  USO:
#     bash scripts/run_experiments.sh            # corre fase 1 + fase 2
#     FASE=1 bash scripts/run_experiments.sh     # solo fase 1
#     FASE=2 bash scripts/run_experiments.sh     # solo fase 2
#
#  Requiere haber compilado antes con `make` (ejecutable ./gap_simulator).
# ===========================================================================

set -u
cd "$(dirname "$0")/.."   # raiz del proyecto, asi los paths relativos funcionan

BIN=./gap_simulator
OUT_DIR=results
SOL_DIR=out/exp
CSV="$OUT_DIR/experiments.csv"
FASE="${FASE:-12}"        # "1", "2" o "12" (ambas)

mkdir -p "$OUT_DIR" "$SOL_DIR"

if [ ! -x "$BIN" ]; then
    echo "ERROR: no existe $BIN. Compilar primero con 'make'." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
#  Instancias del experimento: TODAS las instancias benchmark provistas.
#  (El enunciado pide experimentacion exhaustiva con las instancias de
#  benchmark; la instancia real se analiza por separado en el inciso 5.)
# ---------------------------------------------------------------------------
INSTANCES=$(find instances/gap -type f | sort)

# ---------------------------------------------------------------------------
#  Factores experimentales.
#
#  SEEDS: 5 semillas para estimar media y desvio del ILS. Mas semillas dan
#  mejor estimacion pero multiplican el tiempo total; 5 alcanza para
#  detectar configuraciones claramente mejores o claramente inestables.
#
#  ITERS: rango logaritmico para ver rendimientos decrecientes (esperamos
#  que la mejora marginal caiga con las iteraciones).
#
#  STRENGTH_DIVS: la intensidad se expresa como fraccion de n (n/100 suave
#  ... n/10 agresiva) para que sea comparable entre instancias de distinto
#  tamanio. El script la convierte a un entero por instancia.
#
#  ACCEPTS: 'better' = ILS clasico de la teorica; 'sa' = nuestra variante
#  con aceptacion probabilistica tipo annealing. La comparacion directa
#  entre ambos es uno de los resultados centrales del informe.
# ---------------------------------------------------------------------------
SEEDS="1 2 3 4 5"
ITERS="50 100 250 500"
STRENGTH_DIVS="100 50 20 10"
ACCEPTS="better sa"

# Encabezado del CSV (coincide con la fila que emite main.cpp en modo --csv,
# precedida por la columna 'fase').
echo "fase,instancia,algoritmo,metodo,iteraciones,strength,accept,semilla,costo,sin_asignar,ms,ok" > "$CSV"

run() {
    # run <fase> <instancia> <metodo> <iters> <semilla> <strength> <accept>
    local fase="$1" inst="$2" met="$3" it="$4" seed="$5" str="$6" acc="$7"
    local tag
    tag=$(echo "$inst" | tr '/' '_')
    local line
    line=$("$BIN" "$inst" "$SOL_DIR/${tag}_${met}_${it}_${str}_${acc}_${seed}.sol" \
                  "$met" "$it" "$seed" "$str" "$acc" --csv) || true
    [ -n "$line" ] && echo "$fase,$line" >> "$CSV"
}

# ---------------------------------------------------------------------------
#  FASE 1: comparacion de metodos.
#  Los metodos sin azar se corren UNA vez (semilla irrelevante); el ILS
#  default se corre con todas las semillas para tener barras de error.
# ---------------------------------------------------------------------------
if [[ "$FASE" == *1* ]]; then
    echo ">> FASE 1: comparacion de metodos"
    for inst in $INSTANCES; do
        echo "   $inst"
        for met in greedy regret shift swap shift_swap; do
            run 1 "$inst" "$met" 0 1 0 sa
        done
        for seed in $SEEDS; do
            run 1 "$inst" ils 100 "$seed" 0 sa     # strength 0 => default n/50
        done
    done
fi

# ---------------------------------------------------------------------------
#  FASE 2: tuning del ILS (grilla completa).
#  4 iteraciones x 4 strengths x 2 criterios x 5 semillas = 160 corridas
#  por instancia. Si el tiempo total resulta excesivo, reducir primero el
#  numero de instancias (una representativa por familia a/b/e), NO las
#  semillas: sin repeticiones no hay barras de error.
# ---------------------------------------------------------------------------
if [[ "$FASE" == *2* ]]; then
    echo ">> FASE 2: tuning del ILS"
    for inst in $INSTANCES; do
        echo "   $inst"
        # n = segundo numero de la primera linea de la instancia; lo usamos
        # para traducir el divisor de strength a un entero >= 1.
        n=$(awk 'NR==1{print $2; exit}' "$inst")
        for it in $ITERS; do
            for div in $STRENGTH_DIVS; do
                str=$(( n / div )); [ "$str" -lt 1 ] && str=1
                for acc in $ACCEPTS; do
                    for seed in $SEEDS; do
                        run 2 "$inst" ils "$it" "$seed" "$str" "$acc"
                    done
                done
            done
        done
    done
fi

echo ">> Listo. Resultados en $CSV ($(($(wc -l < "$CSV") - 1)) filas)."
echo ">> Filas con ok=0 indican un bug y deben investigarse, no promediarse."

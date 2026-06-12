# TP2 - Logistica centralizada de primera milla

Resolvedor del **Generalized Assignment Problem (GAP)** para el caso ThunderPack.

## Estructura del proyecto

```text
td5_tp2/
|-- main.cpp            # punto de entrada y algoritmos principales
|-- Makefile            # compila el ejecutable gap_simulator
|-- README.md
|-- src/
|   |-- instance.h      # lectura de instancias y datos del problema
|   |-- solution.h      # solucion, costo, factibilidad y movimientos basicos
|   `-- io.h            # escritura de la solucion en el formato pedido
|-- instances/
|   |-- gap/            # instancias benchmark tipo OR-Library
|   `-- real/           # instancia real del TP
`-- out/                # salidas generadas localmente
```

## Compilacion

```bash
make
```

Eso genera el ejecutable `gap_simulator`.

Para limpiar objetos y ejecutable:

```bash
make clean
```

## Ejecucion

```bash
<<<<<<< HEAD
./gap_simulator <archivo_instancia> <archivo_salida> [algoritmo] [iteraciones] [semilla] [strength] [accept] [--csv]
```

Parametros opcionales:

- `iteraciones`: iteraciones del ILS (default 100).
- `semilla`: semilla del generador aleatorio del ILS (default 1234567).
- `strength`: cantidad de movimientos aleatorios por perturbacion del ILS.
  Con `0` (o ausente) se usa el default `n/50`.
- `accept`: criterio de aceptacion del incumbent en el ILS: `better`
  (solo acepta mejoras, ILS clasico) o `sa` (acepta peoras con
  probabilidad `exp(-delta/T)`, temperatura decreciente; default).
- `--csv`: en lugar del reporte legible, imprime una unica linea CSV
  `instancia,algoritmo,metodo,iteraciones,strength,accept,semilla,costo,sin_asignar,ms,ok`
  para sistematizar la experimentacion.

## Experimentacion (inciso 4)

```bash
make
bash scripts/run_experiments.sh        # fase 1 (metodos) + fase 2 (tuning ILS)
FASE=1 bash scripts/run_experiments.sh # solo comparacion de metodos
```

Los resultados quedan en `results/experiments.csv`, una fila por corrida.

=======
./gap_simulator <archivo_instancia> <archivo_salida> [algoritmo] [iteraciones] [semilla]
```

>>>>>>> bc77822 (Entrega TP2)
En Windows PowerShell:

```powershell
.\gap_simulator.exe <archivo_instancia> <archivo_salida> [algoritmo] [iteraciones] [semilla]
```

Ejemplos:

```bash
./gap_simulator instances/gap/gap_a/a05100 out/a05100.sol ils 100 123
./gap_simulator instances/gap/gap_a/a05100 out/a05100_shift_swap.sol shift_swap
./gap_simulator instances/real/real_instance out/real.sol ils 300 123
```

Si no se indica algoritmo, se usa `ils` con 100 iteraciones y semilla fija.

## Algoritmos disponibles

- `greedy`: heuristica constructiva golosa por costo, procesando primero vendedores de mayor demanda.
- `regret`: heuristica constructiva por arrepentimiento, priorizando vendedores que perderian mucho si no se asignan ahora.
- `shift`: parte de la mejor constructiva y aplica busqueda local por movimiento individual de vendedor.
- `swap`: parte de la mejor constructiva y aplica busqueda local por intercambio de dos vendedores asignados.
- `shift_swap`: combina los dos operadores de busqueda local hasta que ninguno mejore.
- `ils`: metaheuristica Iterated Local Search con perturbacion y busqueda local `shift_swap`.

## Formato de instancia

```text
m n
matriz de costos c_ij      (m filas, n columnas)
matriz de demandas d_ij    (m filas, n columnas)
capacidades de depositos   (m valores)
```

Los costos pueden ser enteros o decimales. Las demandas y capacidades se leen como enteros.

## Formato de salida

El archivo de salida tiene una linea por deposito. En la linea `i` aparecen los vendedores asignados a ese deposito, separados por espacios.

La base de indices se define en `src/io.h` mediante `OUTPUT_INDEX_BASE`.

## Funcion objetivo

El costo de una solucion es:

```text
suma de costos de asignacion + penalizacion por vendedores sin asignar
```

La penalizacion usada por el codigo es `3 * cmax`, donde `cmax` es el mayor costo de la instancia.

## Estado actual

Implementado:

- lectura de instancias;
- escritura de soluciones;
- clase de instancia;
- clase de solucion;
- comparacion de soluciones con `operator<`;
- funcion de costo incremental y validacion por recomputo;
- chequeo de factibilidad por capacidades;
- dos heuristicas constructivas;
- dos operadores de busqueda local;
- metaheuristica ILS.

<<<<<<< HEAD
- parametros de tuning del ILS expuestos por CLI (`strength`, `accept`);
- modo `--csv` y script `scripts/run_experiments.sh` para la
  experimentacion del inciso 4.

Pendiente:

- correr la grilla completa y armar tablas/graficos;
- analizar resultados de benchmark y caso real (inciso 5);
=======
Pendiente:

- sistematizar experimentacion y tuning;
- analizar resultados de benchmark y caso real;
>>>>>>> bc77822 (Entrega TP2)
- redactar el informe.

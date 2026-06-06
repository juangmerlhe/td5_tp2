# TP2 - Logística centralizada de primera milla (GAP / ThunderPack)

Resolvedor del **Generalized Assignment Problem (GAP)** para el caso ThunderPack.

## Estructura del proyecto

```
gap/
├── main.cpp            # punto de entrada (CLI) -- "aca empieza la magia"
├── Makefile            # provisto por la catedra
├── README.md
├── src/
│   ├── instance.h      # lectura del archivo de instancia + estructuras de datos
│   ├── solution.h      # representacion de solucion, costo y factibilidad (incremental)
│   └── io.h            # escritura de la solucion en el formato pedido
├── instances/          # instancias (benchmark y real)
│   ├── gap/{gap_a,gap_b,gap_e}/...
│   └── real/real_instance
└── out/                # archivos de salida generados
```

> **Diseño header-only.** Toda la logica vive en los headers de `src/`, que
> `main.cpp` incluye. Por eso el Makefile (que compila un unico archivo fuente,
> `main.cpp`) funciona sin modificaciones: no hay que agregar `.cpp` a `SRC`.

## Compilación

Requiere `g++` con soporte C++17. Usando el Makefile provisto:

```bash
make          # compila el ejecutable ./gap_simulator
make clean    # borra objetos y ejecutable
```

## Ejecución

```bash
./gap_simulator <archivo_instancia> <archivo_salida>
```

Ejemplos:

```bash
./gap_simulator instances/gap/gap_a/a05100 out/a05100.sol
./gap_simulator instances/real/real_instance out/real.sol
```

El programa imprime estadísticas de la instancia y, en esta etapa (Fase 0),
corre un asignador trivial de prueba que será reemplazado por las heurísticas
constructivas reales en la Fase 1.

## Recomendación para la competencia (optimización)

El Makefile provisto compila **sin optimización** (`CFLAGS = -std=c++17`). Para
la instancia real y la metaheurística (que corre durante minutos), activar `-O2`
da una aceleración de ~5-10x. Es solo un *flag* del compilador (no una librería
externa), así que es válido. Cambiar una línea:

```make
CFLAGS = -std=c++17 -O2
```

Para depurar durante el desarrollo (detecta accesos invalidos y comportamiento
indefinido), compilar a mano sin tocar el Makefile:

```bash
g++ -std=c++17 -O0 -g -fsanitize=address,undefined main.cpp -o gap_simulator
```

## Formato de la instancia

```
Linea 1 : m n                  (m = depositos, n = vendedores)
Luego   : m*n valores  costos   c_ij  (fila i = costos del deposito i)
Luego   : m*n valores  demandas d_ij  (fila i = demandas del deposito i)
Luego   : m valores    capacidades c_i
```

En el benchmark todos los valores son enteros; en la instancia real los costos
son decimales (distancias). El lector maneja ambos casos (lee costos como `double`).

## Formato de salida

Una línea por depósito (la línea *i* corresponde al depósito *i*), con los
índices de los vendedores asignados separados por espacios.

> **Base de índices:** definida por la constante `OUTPUT_INDEX_BASE` en
> `src/io.h` (por defecto 0). El enunciado modela N = {1,...,n}; **confirmar con
> la cátedra** si esperan salida base-0 o base-1 y ajustar esa constante.

## Función objetivo

Suma de los costos (distancias) de las asignaciones, más una penalización de
`3 * cmax` por cada vendedor sin asignar, donde `cmax` es la máxima distancia
de la instancia.

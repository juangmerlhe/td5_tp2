// ===========================================================================
//  main.cpp  --  Punto de entrada del resolvedor del GAP (ThunderPack / TP2)
// ===========================================================================
//
//  USO:
//     ./gap <archivo_instancia> <archivo_salida>
//
//  Ejemplo:
//     ./gap instances/gap/gap_a/a05100 out/a05100.sol
//
//  ----------------------------------------------------------------------------
//  ESTADO: FASE 0 (infraestructura).
//  ----------------------------------------------------------------------------
//  Esta version solo:
//     1) lee la instancia,
//     2) imprime estadisticas utiles,
//     3) corre un asignador TRIVIAL (placeholder) para demostrar que el
//        pipeline completo funciona y produce un archivo de salida valido,
//     4) valida que el costo incremental coincide con el recalculo desde cero,
//     5) escribe la solucion.
//
//  El placeholder de la Fase 0 se REEMPLAZA en la Fase 1 por las heuristicas
//  constructivas de verdad (greedy por costo, greedy con regret, etc.).
// ===========================================================================

#include "src/instance.h"
#include "src/solution.h"
#include "src/io.h"

#include <iostream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <numeric>   // para std::iota
#include <algorithm> // para std::sort

// ---------------------------------------------------------------------------
//  HEURISTICA CONSTRUCTIVA 1: Greedy por costo con orden por demanda.
//
//  Idea: procesar los vendedores de MAYOR a MENOR demanda y asignar cada uno
//  a su deposito factible mas barato. Se priorizan los "grandes" porque son
//  los mas dificiles de ubicar: si se los deja para el final, los depositos ya
//  estan llenos y caen en penalizacion. Los chicos entran despues en los huecos.
//
//  Detalle del GAP general (formato de las matrices):
//  La demanda es d_ij, es decir, depende del deposito i. Entonces un vendedor j
//  NO tiene una unica demanda sino una por deposito (la columna j de demand).
//  Para poder ORDENAR necesitamos un solo numero por vendedor, asi que tomamos
//  un representante: el MAXIMO sobre los depositos (peor caso de ocupacion).
//  En la instancia real d_ij = d_j (todas las filas de demand son iguales), por
//  lo que ese maximo coincide con la demanda unica del vendedor: la version
//  general no rompe el caso real y ademas funciona en las instancias benchmark.
//
//  Tecnica de ordenamiento: NO se reordenan las matrices (la columna de un
//  vendedor vive en cost/demand y moverla seria caro y romperia los indices de
//  salida). Se ordena un vector de INDICES y se visita a los vendedores en ese
//  orden; los datos quedan intactos.
//
//  Complejidad: O(n*m) para el representante de demanda + O(n log n) del sort
//  + O(n*m) del loop de asignacion  =>  O(n*m + n log n).
// ---------------------------------------------------------------------------

static Solution greedy_por_costo(const Instance& I) {
    Solution sol(I);

    // Representante de demanda por vendedor: maximo sobre depositos.
    // En la instancia real (d_ij = d_j) coincide con la demanda unica.
    std::vector<int64_t> dj(I.n);
    for (int j = 0; j < I.n; ++j) {
        int64_t mx = 0;
        for (int i = 0; i < I.m; ++i)
            mx = std::max(mx, I.demand[i][j]);
        dj[j] = mx;
    }

    // Orden de visita: vendedores de mayor a menor demanda.
    std::vector<int> order(I.n);                // order[j] = indice del vendedor que va en la posicion j del orden
    std::iota(order.begin(), order.end(), 0);   // inicialmente order = [0, 1, 2, ..., n-1] 
    std::sort(order.begin(), order.end(),       
              [&](int a, int b) { return dj[a] > dj[b]; }); // sort con comparador personalizado: ordena por demanda descendente

    // Asignacion golosa: a cada vendedor, su deposito factible mas barato.
    for (int idx = 0; idx < I.n; ++idx) {
        int j = order[idx];                // vendedor a asignar
        int best_i = Solution::UNASSIGNED; // mejor deposito encontrado hasta ahora para j
        double best_c = 0.0;               // costo del mejor deposito encontrado hasta ahora para j
        for (int i = 0; i < I.m; ++i) {    // iteramos sobre los depositos para encontrar el mejor para j
            if (sol.can_assign(j, i)) {
                if (best_i == Solution::UNASSIGNED || I.cost[i][j] < best_c) {
                    best_i = i;
                    best_c = I.cost[i][j];
                }
            }
        }
        if (best_i != Solution::UNASSIGNED) sol.do_assign(j, best_i);
    }
    return sol;
}

// ---------------------------------------------------------------------------
//  HEURISTICA CONSTRUCTIVA 2: Greedy con "arrepentimiento" (regret / look-ahead).
//
//  Idea: la heuristica 1 fija el ORDEN de antemano (por demanda) y nunca lo
//  revisa. Eso es miope: un vendedor "comodo" hoy puede quedar atrapado mas
//  tarde porque su mejor deposito se lleno mientras atendiamos a otros, y cae
//  en penalizacion. La heuristica de arrepentimiento corrige esto preguntando,
//  en CADA paso: "quien es el que mas pierde si NO lo atiendo ahora?".
//
//  Para cada vendedor j sin asignar miramos sus DOS depositos factibles mas
//  baratos: el mejor (costo c1) y el segundo mejor (costo c2). Definimos el
//  arrepentimiento como
//        regret(j) = c2 - c1.
//  Regret grande  => "si pierdo mi mejor opcion, la siguiente es mucho peor":
//                     vendedor urgente, lo atendemos ya.
//  Regret chico   => "me da casi igual donde caer": puede esperar.
//  En cada iteracion asignamos al vendedor de MAYOR regret a su deposito mas
//  barato y recalculamos, porque las capacidades cambiaron.
//
//  Casos borde:
//   - Un solo deposito factible  -> regret = +infinito. Es lo mas urgente: si
//     ese unico deposito se llena, el vendedor cae si o si en penalizacion.
//   - Cero depositos factibles    -> el vendedor es INASIGNABLE. Como en una
//     constructiva las capacidades solo decrecen (nunca liberamos), nunca
//     volvera a entrar: lo marcamos "muerto" y deja de competir. Queda sin
//     asignar y paga 3*cmax, igual que en H1.
//
//  Por que es DISTINTA de H1 (y no solo "otro orden"):
//   - H1 ordena UNA vez por un atributo fijo del vendedor (demanda) y asigna.
//   - H2 NO tiene orden fijo: el orden EMERGE del estado de las capacidades.
//     Es una decision con mirada hacia adelante (look-ahead) que reacciona a
//     como se va llenando la red. Es la heuristica clasica de Martello-Toth.
//
//  Tecnica: igual que en H1 no tocamos las matrices. Mantenemos un vector
//  alive[] que marca quien sigue compitiendo (no asignado y aun asignable).
//
//  Complejidad: en cada uno de los (a lo sumo) n pasos recorremos los vendedores
//  vivos y, por cada uno, sus m depositos para hallar c1 y c2  =>  O(n*m) por
//  paso, O(n^2 * m) en total. Trivial en benchmark; ver NOTA al final para la
//  instancia real (n=1100, m=310).
// ---------------------------------------------------------------------------

static Solution greedy_regret(const Instance& I) {
    Solution sol(I);

    // alive[j] == 1  -> el vendedor j sigue compitiendo por un deposito.
    // Pasa a 0 cuando j ya fue asignado, o cuando se queda sin ningun deposito
    // factible (inasignable para siempre: las capacidades solo bajan).
    std::vector<char> alive(I.n, 1);
    int remaining = I.n;   // cantidad de vendedores todavia "vivos"

    while (remaining > 0) {
        int    best_j      = Solution::UNASSIGNED;  // vendedor a asignar este paso
        int    best_depot  = Solution::UNASSIGNED;  // su deposito mas barato (i1)
        double best_regret = -1.0;                  // regret del elegido
        double best_c1     = 0.0;                   // c1 del elegido (para desempate)

        for (int j = 0; j < I.n; ++j) {
            if (!alive[j]) continue;

            // Buscamos los DOS depositos factibles mas baratos para j en una
            // sola pasada: i1/c1 = el mejor, i2/c2 = el segundo mejor.
            int i1 = Solution::UNASSIGNED; double c1 = 0.0;
            int i2 = Solution::UNASSIGNED; double c2 = 0.0;
            for (int i = 0; i < I.m; ++i) {
                if (!sol.can_assign(j, i)) continue;
                double c = I.cost[i][j];
                if (i1 == Solution::UNASSIGNED || c < c1) {
                    i2 = i1; c2 = c1;   // el viejo mejor pasa a ser el segundo
                    i1 = i;  c1 = c;
                } else if (i2 == Solution::UNASSIGNED || c < c2) {
                    i2 = i;  c2 = c;
                }
            }

            if (i1 == Solution::UNASSIGNED) {
                // Sin deposito factible: inasignable para siempre. Lo retiramos.
                alive[j] = 0;
                --remaining;
                continue;
            }

            // Con un solo deposito factible -> +infinito (maxima urgencia).
            double regret = (i2 == Solution::UNASSIGNED)
                          ? std::numeric_limits<double>::infinity()
                          : (c2 - c1);

            // Nos quedamos con el de mayor regret. Desempate: menor c1 (atendemos
            // primero al que ademas tiene la opcion mas barata; criterio simple
            // y reproducible).
            if (regret > best_regret ||
               (regret == best_regret && c1 < best_c1)) {
                best_regret = regret;
                best_j      = j;
                best_depot  = i1;
                best_c1     = c1;
            }
        }

        if (best_j == Solution::UNASSIGNED) break;  // no quedan asignables

        sol.do_assign(best_j, best_depot);
        alive[best_j] = 0;
        --remaining;
    }

    return sol;
}

// ---------------------------------------------------------------------------
//  Helper de experimentacion: corre una heuristica, valida y reporta.
// ---------------------------------------------------------------------------
struct Report { double cost; int unassigned; double ms; bool ok; };

static Report run_and_report(const char* name,
                             Solution (*fn)(const Instance&),
                             const Instance& I) {
    auto t0 = std::chrono::steady_clock::now();
    Solution sol = fn(I);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    double inc   = sol.cost();
    double fresh = sol.recompute_cost_from_scratch();
    bool ok = (std::fabs(inc - fresh) < 1e-6) && sol.is_feasible();

    std::cout << "\n---- " << name << " ----\n";
    std::cout << "  asignados   : " << (I.n - sol.num_unassigned) << " / " << I.n << "\n";
    std::cout << "  sin asignar : " << sol.num_unassigned << "\n";
    std::cout << "  costo       : " << inc << "\n";
    std::cout << "  factible+ok : " << (ok ? "SI" : "NO  <-- BUG") << "\n";
    std::cout << "  tiempo      : " << ms << " ms\n";
    return { inc, sol.num_unassigned, ms, ok };
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);

    if (argc < 3) {
        std::cerr << "Uso: " << argv[0] << " <archivo_instancia> <archivo_salida> [greedy|regret]\n";
        std::cerr << "Ej : " << argv[0] << " instances/gap/gap_a/a05100 out/a05100.sol\n";
        std::cerr << "Sin el 3er parametro corre AMBAS heuristicas y escribe la mejor.\n";
        return 1;
    }
    const std::string in_path  = argv[1];
    const std::string out_path = argv[2];
    const std::string method   = (argc >= 4) ? argv[3] : "";

    Instance I;
    try {
        I = Instance::read(in_path);
    } catch (const std::exception& e) {
        std::cerr << "ERROR leyendo la instancia: " << e.what() << "\n";
        return 1;
    }

    // --- Estadisticas de la instancia -------------------------------------
    const int64_t cap_total = I.total_capacity();
    const int64_t dem_min   = I.min_total_demand();
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "==== Instancia: " << in_path << " ====\n";
    std::cout << "  depositos (m)          : " << I.m << "\n";
    std::cout << "  vendedores (n)         : " << I.n << "\n";
    std::cout << "  capacidad total        : " << cap_total << "\n";
    std::cout << "  demanda total (minima) : " << dem_min << "\n";
    std::cout << "  holgura agregada       : " << (cap_total - dem_min)
              << (dem_min > cap_total ? "  (INSUFICIENTE)" : "  (alcanza en agregado)") << "\n";
    std::cout << "  cmax                   : " << I.cmax << "\n";
    std::cout << "  penalizacion (3*cmax)  : " << I.penalty_per_unassigned << "\n";

    // --- Elegir y correr la(s) heuristica(s) ------------------------------
    Solution sol(I);
    if (method == "greedy") {
        run_and_report("Greedy por costo", greedy_por_costo, I);
        sol = greedy_por_costo(I);
    } else if (method == "regret") {
        run_and_report("Greedy con regret", greedy_regret, I);
        sol = greedy_regret(I);
    } else {
        // Sin parametro: corremos ambas, comparamos y escribimos la mejor.
        Report rg = run_and_report("Greedy por costo",  greedy_por_costo, I);
        Report rr = run_and_report("Greedy con regret", greedy_regret,    I);
        bool regret_gana = rr.cost < rg.cost;
        std::cout << "\n>> Mejor: " << (regret_gana ? "Greedy con regret" : "Greedy por costo")
                  << " (se escribe esta solucion)\n";
        sol = regret_gana ? greedy_regret(I) : greedy_por_costo(I);
    }

    // --- Escritura de la solucion -----------------------------------------
    try {
        write_solution(out_path, sol);
    } catch (const std::exception& e) {
        std::cerr << "ERROR escribiendo la salida: " << e.what() << "\n";
        return 1;
    }
    std::cout << "\nSolucion escrita en: " << out_path << "\n";
    return 0;
}

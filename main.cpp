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
//  HEURISTICA CONSTRUCTIVA 2: Greedy con regret (arrepentimiento).
//
//  Idea: a diferencia de la greedy por costo (que fija el orden de visita de
//  antemano), aca NO hay orden prefijado. En cada paso se decide DINAMICAMENTE
//  a que vendedor conviene asignar ahora, mirando el estado actual (que
//  depositos siguen con capacidad).
//
//  El "regret" (arrepentimiento) de un vendedor mide cuanto pierde si NO consigue
//  su mejor deposito y tiene que ir al segundo mejor:
//        regret(j) = costo(2do mejor deposito factible) - costo(mejor factible)
//  Un regret grande significa "este vendedor es URGENTE": si lo dejo para
//  despues y su mejor deposito se llena, el salto de costo es enorme. Un regret
//  chico significa que puede esperar sin gran perdida. En cada paso asignamos al
//  de MAYOR regret a su mejor deposito factible, y recalculamos.
//
//  Casos borde:
//    - 1 solo deposito factible  -> no hay "segundo", regret = +infinito
//      (maximamente urgente: o entra ahi o se pierde).
//    - 0 depositos factibles     -> ya no entra en ningun lado; queda sin
//      asignar (penalizacion) y se descarta de los pendientes.
//
//  Por que recalcular en cada paso: al asignar a alguien, la carga de UN deposito
//  sube, asi que la factibilidad solo puede pasar de "entra" a "no entra", nunca
//  al reves. Por eso un vendedor sin opciones factibles ahora no las recuperara,
//  y los mejores/segundos mejores de los demas pueden cambiar.
//
//  Diferencia estructural con la greedy: la greedy hace UN solo barrido en orden
//  fijo; regret es un bucle que en cada vuelta recalcula prioridades y asigna UN
//  vendedor, hasta que no quede ninguno asignable.
//
//  Complejidad (version base, sin optimizar): cada iteracion escanea todos los
//  pendientes (O(n)) y para cada uno revisa los m depositos (O(m)); hay hasta n
//  iteraciones  =>  O(n^2 * m). Suficiente para las instancias benchmark; para la
//  instancia real (n=1100, m=310) es lenta. La optimizacion (recalcular solo los
//  vendedores afectados por el deposito que cambio) se deja para una version
//  posterior; no cambia el resultado, solo el tiempo.
// ---------------------------------------------------------------------------
static Solution greedy_regret(const Instance& I) {
    Solution sol(I);

    const double INF = std::numeric_limits<double>::infinity();

    // dead[j] = 1 si el vendedor j ya no tiene ningun deposito factible
    // (queda sin asignar definitivamente; paga penalizacion).
    std::vector<char> dead(I.n, 0);

    while (true) {
        int    chosen   = Solution::UNASSIGNED;  // vendedor elegido este paso
        int    chosen_i = Solution::UNASSIGNED;  // su mejor deposito factible
        double best_regret = -1.0;               // mayor regret visto (>= 0, o INF)

        // --- Buscar al vendedor pendiente con mayor regret ---
        for (int j = 0; j < I.n; ++j) {
            if (sol.assign[j] != Solution::UNASSIGNED || dead[j]) continue; // ya resuelto

            // Mejor y segundo mejor deposito FACTIBLE para j.
            int    best_i   = Solution::UNASSIGNED;
            double best_c   = 0.0;   // costo del mejor    (valido si feasible >= 1)
            double second_c = 0.0;   // costo del 2do mejor (valido si feasible >= 2)
            int    feasible = 0;

            for (int i = 0; i < I.m; ++i) {
                if (!sol.can_assign(j, i)) continue;
                double c = I.cost[i][j];
                if (feasible == 0 || c < best_c) {
                    second_c = best_c;   // el viejo mejor pasa a ser el segundo
                    best_c   = c;
                    best_i   = i;
                } else if (feasible == 1 || c < second_c) {
                    second_c = c;        // candidato a segundo mejor
                }
                ++feasible;
            }

            if (feasible == 0) { dead[j] = 1; continue; } // sin opciones -> descartado

            double regret = (feasible == 1) ? INF : (second_c - best_c);

            if (regret > best_regret) {   // empate: gana el de menor indice (simple)
                best_regret = regret;
                chosen      = j;
                chosen_i    = best_i;
            }
        }

        if (chosen == Solution::UNASSIGNED) break; // no queda nadie asignable

        sol.do_assign(chosen, chosen_i);
    }

    return sol;
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);

    if (argc < 3) {
        std::cerr << "Uso: " << argv[0] << " <archivo_instancia> <archivo_salida>\n";
        std::cerr << "Ej : " << argv[0] << " instances/gap/gap_a/a05100 out/a05100.sol\n";
        return 1;
    }
    const std::string in_path  = argv[1];
    const std::string out_path = argv[2];

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
    std::cout << "  depositos (m)            : " << I.m << "\n";
    std::cout << "  vendedores (n)           : " << I.n << "\n";
    std::cout << "  capacidad total          : " << cap_total << "\n";
    std::cout << "  demanda total (minima)   : " << dem_min << "\n";
    std::cout << "  holgura agregada         : " << (cap_total - dem_min)
              << (dem_min > cap_total ? "  (INSUFICIENTE: imposible asignar a todos)" : "  (alcanza en agregado)") << "\n";
    std::cout << "  cmax                     : " << I.cmax << "\n";
    std::cout << "  penalizacion (3*cmax)    : " << I.penalty_per_unassigned << "\n";

    // --- Placeholder Fase 0 -----------------------------------------------
    auto t0 = std::chrono::steady_clock::now();
    Solution sol = trivial_first_fit(I);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // --- Validacion: costo incremental vs recalculo desde cero ------------
    double inc   = sol.cost();
    double fresh = sol.recompute_cost_from_scratch();
    bool ok_cost = std::fabs(inc - fresh) < 1e-6;
    bool ok_feas = sol.is_feasible();

    std::cout << "\n---- Resultado (placeholder Fase 0) ----\n";
    std::cout << "  asignados                : " << (I.n - sol.num_unassigned) << " / " << I.n << "\n";
    std::cout << "  sin asignar              : " << sol.num_unassigned << "\n";
    std::cout << "  costo (incremental)      : " << inc << "\n";
    std::cout << "  costo (recalculado)      : " << fresh << "\n";
    std::cout << "  costo consistente?       : " << (ok_cost ? "SI" : "NO  <-- BUG") << "\n";
    std::cout << "  factible?                : " << (ok_feas ? "SI" : "NO  <-- BUG") << "\n";
    std::cout << "  tiempo placeholder       : " << ms << " ms\n";

    // --- Escritura de la solucion -----------------------------------------
    try {
        write_solution(out_path, sol);
    } catch (const std::exception& e) {
        std::cerr << "ERROR escribiendo la salida: " << e.what() << "\n";
        return 1;
    }
    std::cout << "\nSolucion escrita en: " << out_path << "\n";

    return (ok_cost && ok_feas) ? 0 : 2;
}

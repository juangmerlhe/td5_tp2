// ===========================================================================
//  main.cpp  --  Punto de entrada del resolvedor del GAP (ThunderPack / TP2)
// ===========================================================================
//
//  USO:
//     ./gap_simulator <archivo_instancia> <archivo_salida> [algoritmo] [iteraciones] [semilla]
//
//  Ejemplo:
//     ./gap_simulator instances/gap/gap_a/a05100 out/a05100.sol ils 100 123
//
//  ----------------------------------------------------------------------------
//  ESTADO: heuristicas constructivas, busqueda local e ILS.
//  ----------------------------------------------------------------------------
//  Algoritmos disponibles:
//     greedy, regret, shift, swap, shift_swap, ils.
// ===========================================================================

#include "src/instance.h"
#include "src/solution.h"
#include "src/io.h"

#include <iostream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <numeric>   // para std::iota
#include <algorithm> // para std::sort
#include <random>
#include <string>

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
//  Helper de reporte: corre una heuristica, valida y reporta.
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

// ---------------------------------------------------------------------------
//  OPERADOR DE BUSQUEDA LOCAL 1: SHIFT / RELOCATE.
//
//  Vecindario: mover un vendedor j desde su deposito actual a otro deposito i.
//  Tambien contempla vendedores sin asignar: moverlos desde UNASSIGNED a un
//  deposito factible cuenta como una mejora si evita pagar la penalizacion.
//
//  Estrategia: best improvement. En cada iteracion se revisan todos los pares
//  (vendedor, deposito destino), se aplica el movimiento con mayor reduccion de
//  costo y se repite hasta llegar a un optimo local para este vecindario.
// ---------------------------------------------------------------------------
static int local_search_shift_best_improvement(Solution& sol) {
    const Instance& I = *sol.inst;
    const double EPS = 1e-9;
    int total_moves = 0;

    while (true) {
        int best_j = Solution::UNASSIGNED;
        int best_i = Solution::UNASSIGNED;
        double best_delta = -EPS;

        for (int j = 0; j < I.n; ++j) {
            int i_old = sol.assign[j];
            double old_cost = (i_old == Solution::UNASSIGNED)
                            ? I.penalty_per_unassigned
                            : I.cost[i_old][j];

            for (int i_new = 0; i_new < I.m; ++i_new) {
                if (i_new == i_old) continue;
                if (sol.load[i_new] + I.demand[i_new][j] > I.capacity[i_new]) continue;

                double delta = I.cost[i_new][j] - old_cost;
                if (delta < best_delta) {
                    best_delta = delta;
                    best_j = j;
                    best_i = i_new;
                }
            }
        }

        if (best_j == Solution::UNASSIGNED) break;
        sol.move_to(best_j, best_i);
        ++total_moves;
    }

    return total_moves;
}

// ---------------------------------------------------------------------------
//  OPERADOR DE BUSQUEDA LOCAL 2: SWAP.
//
//  Vecindario: intercambiar las asignaciones de dos vendedores a y b que estan
//  en depositos distintos ia e ib.
//    a pasa de ia a ib
//    b pasa de ib a ia
//
//  Como la demanda depende del deposito (demand[i][j]), la factibilidad debe
//  chequearse con las cargas resultantes:
//    new_load_ia = load[ia] - demand[ia][a] + demand[ia][b]
//    new_load_ib = load[ib] - demand[ib][b] + demand[ib][a]
//
//  Delta de costo (negativo = mejora):
//    delta = cost[ia][b] + cost[ib][a] - cost[ia][a] - cost[ib][b]
//
//  Estrategia: best improvement. En cada iteracion se revisan todos los pares
//  (a, b) con a < b, ia != ib, ambos asignados; se aplica el swap factible con
//  mayor reduccion de costo y se repite hasta llegar a optimo local.
// ---------------------------------------------------------------------------
static int local_search_swap_best_improvement(Solution& sol) {
    const Instance& I = *sol.inst;
    const double EPS = 1e-9;
    int total_swaps = 0;

    while (true) {
        int best_a = Solution::UNASSIGNED;
        int best_b = Solution::UNASSIGNED;
        double best_delta = -EPS;

        for (int a = 0; a < I.n - 1; ++a) {
            int ia = sol.assign[a];
            if (ia == Solution::UNASSIGNED) continue;

            for (int b = a + 1; b < I.n; ++b) {
                int ib = sol.assign[b];
                if (ib == Solution::UNASSIGNED) continue;
                if (ia == ib) continue;

                int64_t new_load_ia = sol.load[ia] - I.demand[ia][a] + I.demand[ia][b];
                int64_t new_load_ib = sol.load[ib] - I.demand[ib][b] + I.demand[ib][a];
                if (new_load_ia > I.capacity[ia]) continue;
                if (new_load_ib > I.capacity[ib]) continue;

                double delta = I.cost[ia][b] + I.cost[ib][a]
                             - I.cost[ia][a] - I.cost[ib][b];
                if (delta < best_delta) {
                    best_delta = delta;
                    best_a = a;
                    best_b = b;
                }
            }
        }

        if (best_a == Solution::UNASSIGNED) break;

        int ia = sol.assign[best_a];
        int ib = sol.assign[best_b];

        sol.load[ia] -= I.demand[ia][best_a];
        sol.load[ia] += I.demand[ia][best_b];
        sol.load[ib] -= I.demand[ib][best_b];
        sol.load[ib] += I.demand[ib][best_a];
        sol.current_cost += best_delta;
        sol.assign[best_a] = ib;
        sol.assign[best_b] = ia;

        ++total_swaps;
    }

    return total_swaps;
}

// ---------------------------------------------------------------------------
//  BUSQUEDA LOCAL COMBINADA: SHIFT + SWAP.
//
//  Aplica ambos vecindarios hasta que ninguno encuentre mejoras. SHIFT suele
//  acomodar vendedores individualmente; SWAP puede destrabar mejoras que no son
//  posibles moviendo un unico vendedor sin violar capacidades.
// ---------------------------------------------------------------------------
static int local_search_shift_swap(Solution& sol) {
    int total_moves = 0;

    while (true) {
        int moves = 0;
        moves += local_search_shift_best_improvement(sol);
        moves += local_search_swap_best_improvement(sol);

        if (moves == 0) break;
        total_moves += moves;
    }

    return total_moves;
}

static Solution best_constructive(const Instance& I) {
    Solution a = greedy_por_costo(I);
    Solution b = greedy_regret(I);
    return (b < a) ? b : a;
}

static void perturb_solution(Solution& sol, std::mt19937& rng, int strength) {
    const Instance& I = *sol.inst;
    if (I.n == 0 || I.m == 0) return;

    std::uniform_int_distribution<int> seller_dist(0, I.n - 1);
    std::uniform_int_distribution<int> depot_dist(0, I.m - 1);
    std::uniform_int_distribution<int> action_dist(0, 3);

    for (int step = 0; step < strength; ++step) {
        int j = seller_dist(rng);

        if (sol.assign[j] != Solution::UNASSIGNED && action_dist(rng) == 0) {
            sol.do_unassign(j);
            continue;
        }

        for (int attempt = 0; attempt < I.m; ++attempt) {
            int i_new = depot_dist(rng);
            if (i_new != sol.assign[j] && sol.move_to(j, i_new)) break;
        }
    }
}

// ---------------------------------------------------------------------------
//  METAHEURISTICA: Iterated Local Search (ILS).
//
//  1) Construye una solucion inicial.
//  2) La lleva a optimo local con SHIFT + SWAP.
//  3) Perturba la solucion actual.
//  4) Vuelve a aplicar SHIFT + SWAP.
//  5) Conserva la mejor solucion encontrada y, segun el criterio de
//     aceptacion elegido, acepta o no soluciones peores como incumbent.
//
//  CRITERIOS DE ACEPTACION (parametro 'accept'):
//
//   - BETTER: el ILS "de libro" visto en clase. El incumbent solo se
//     reemplaza si el candidato es estrictamente mejor. La diversificacion
//     proviene unicamente de la perturbacion.
//
//   - SA: variante inspirada en Simulated Annealing ("aceptar soluciones
//     que empeoran", diapositiva de metaheuristicas). Un candidato peor se
//     acepta con probabilidad exp(-delta/T), con una temperatura T que
//     decrece linealmente a lo largo de la corrida: al principio se acepta
//     casi cualquier cosa (diversificacion), al final casi nada
//     (intensificacion).
//
//  Tener AMBOS criterios como parametro nos permite, en la experimentacion
//  (inciso 4), medir si la aceptacion probabilistica realmente aporta
//  respecto del esquema clasico, en lugar de fijarla a priori.
// ---------------------------------------------------------------------------
enum class AcceptCriterion { BETTER, SA };

static Solution iterated_local_search(const Instance& I,
                                      int iterations,
                                      int perturb_strength,
                                      unsigned seed,
                                      AcceptCriterion accept) {
    Solution current = best_constructive(I);
    local_search_shift_swap(current);

    Solution best = current;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);

    for (int it = 0; it < iterations; ++it) {
        Solution candidate = current;
        perturb_solution(candidate, rng, perturb_strength);
        local_search_shift_swap(candidate);

        if (candidate < best) best = candidate;

        if (candidate < current) {
            current = candidate;
        } else if (accept == AcceptCriterion::SA) {
            double progress = static_cast<double>(it + 1) / std::max(1, iterations);
            double temperature = std::max(1.0, I.penalty_per_unassigned * (1.0 - progress));
            double delta = candidate.cost() - current.cost();
            double accept_prob = std::exp(-delta / temperature);
            if (prob_dist(rng) < accept_prob) current = candidate;
        }
        // Con BETTER, un candidato peor simplemente se descarta: la proxima
        // perturbacion parte otra vez del incumbent actual.
    }

    return best;
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);

    if (argc < 3) {
        std::cerr << "Uso: " << argv[0] << " <archivo_instancia> <archivo_salida> [algoritmo] [iteraciones] [semilla] [strength] [accept] [--csv]\n";
        std::cerr << "Algoritmos: greedy, regret, shift, swap, shift_swap, ils (default)\n";
        std::cerr << "  strength : intensidad de la perturbacion del ILS (default n/50)\n";
        std::cerr << "  accept   : criterio de aceptacion del ILS: better | sa (default sa)\n";
        std::cerr << "  --csv    : imprime UNA linea CSV por stdout en lugar del reporte\n";
        std::cerr << "             (para sistematizar la experimentacion del inciso 4)\n";
        std::cerr << "Ej : " << argv[0] << " instances/gap/gap_a/a05100 out/a05100.sol ils 100 123 22 better --csv\n";
        return 1;
    }
    const std::string in_path  = argv[1];
    const std::string out_path = argv[2];
    const std::string method   = (argc >= 4) ? argv[3] : "";
    const int iterations       = (argc >= 5) ? std::max(0, std::atoi(argv[4])) : 100;
    const unsigned seed        = (argc >= 6) ? static_cast<unsigned>(std::strtoul(argv[5], nullptr, 10)) : 1234567u;

    // strength <= 0 (o ausente) significa "usar el default n/50, calculado
    // luego de leer la instancia" (todavia no conocemos n en este punto).
    const int strength_arg     = (argc >= 7) ? std::atoi(argv[6]) : 0;
    const std::string accept_s = (argc >= 8) ? argv[7] : "sa";

    // Modo CSV: se acepta como ultimo argumento, en cualquier posicion >= 4.
    bool csv_mode = false;
    for (int a = 3; a < argc; ++a)
        if (std::string(argv[a]) == "--csv") csv_mode = true;

    AcceptCriterion accept = AcceptCriterion::SA;
    if (accept_s == "better")  accept = AcceptCriterion::BETTER;
    else if (accept_s != "sa" && accept_s != "--csv") {
        std::cerr << "ERROR: criterio de aceptacion desconocido '" << accept_s << "' (better | sa)\n";
        return 1;
    }

    Instance I;
    try {
        I = Instance::read(in_path);
    } catch (const std::exception& e) {
        std::cerr << "ERROR leyendo la instancia: " << e.what() << "\n";
        return 1;
    }

    // Default de la perturbacion: n/50 vendedores por iteracion (~2% de la
    // instancia). Es el valor historico del proyecto; ahora es tuneable.
    const int perturb_strength = (strength_arg > 0) ? strength_arg
                                                    : std::max(1, I.n / 50);

    // --- Estadisticas de la instancia -------------------------------------
    const int64_t cap_total = I.total_capacity();
    const int64_t dem_min   = I.min_total_demand();
    std::cout << std::fixed << std::setprecision(2);
    if (!csv_mode) {
    std::cout << "==== Instancia: " << in_path << " ====\n";
    std::cout << "  depositos (m)          : " << I.m << "\n";
    std::cout << "  vendedores (n)         : " << I.n << "\n";
    std::cout << "  capacidad total        : " << cap_total << "\n";
    std::cout << "  demanda total (minima) : " << dem_min << "\n";
    std::cout << "  holgura agregada       : " << (cap_total - dem_min)
              << (dem_min > cap_total ? "  (INSUFICIENTE)" : "  (alcanza en agregado)") << "\n";
    std::cout << "  cmax                   : " << I.cmax << "\n";
    std::cout << "  penalizacion (3*cmax)  : " << I.penalty_per_unassigned << "\n";
    } // fin if (!csv_mode)

    // --- Elegir y correr algoritmo ----------------------------------------
    auto t0 = std::chrono::steady_clock::now();
    Solution sol(I);
    std::string algorithm_label;

    if (method == "greedy") {
        sol = greedy_por_costo(I);
        algorithm_label = "Greedy por costo";
    } else if (method == "regret") {
        sol = greedy_regret(I);
        algorithm_label = "Greedy con regret";
    } else if (method == "shift") {
        sol = best_constructive(I);
        local_search_shift_best_improvement(sol);
        algorithm_label = "Mejor constructiva + busqueda local SHIFT";
    } else if (method == "swap") {
        sol = best_constructive(I);
        local_search_swap_best_improvement(sol);
        algorithm_label = "Mejor constructiva + busqueda local SWAP";
    } else if (method == "shift_swap") {
        sol = best_constructive(I);
        local_search_shift_swap(sol);
        algorithm_label = "Mejor constructiva + busqueda local SHIFT+SWAP";
    } else if (method == "ils" || method == "") {
        sol = iterated_local_search(I, iterations, perturb_strength, seed, accept);
        algorithm_label = "Iterated Local Search";
    } else {
        std::cerr << "ERROR: algoritmo desconocido '" << method << "'.\n";
        std::cerr << "Opciones: greedy, regret, shift, swap, shift_swap, ils\n";
        return 1;
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    double inc = sol.cost();
    double fresh = sol.recompute_cost_from_scratch();
    bool ok_cost = std::fabs(inc - fresh) < 1e-6;
    bool ok_feas = sol.is_feasible();

    // ------------------------------------------------------------------
    //  MODO CSV (inciso 4: experimentacion y discusion).
    //
    //  IDEA GENERAL:
    //
    //  Estamos comparando distintos metodos y distintas configuraciones
    //  de ILS para entender como impactan sobre la calidad de la solucion
    //  obtenida y sobre el tiempo de ejecucion.
    //
    //  El objetivo no es solamente obtener una buena solucion, sino
    //  tambien estudiar el comportamiento del algoritmo. Para eso cada
    //  corrida emite UNA fila CSV con todos los factores experimentales
    //  (instancia, metodo, iteraciones, strength, accept, semilla) y las
    //  metricas de respuesta (costo, sin_asignar, tiempo). El script
    //  scripts/run_experiments.sh recorre la grilla y concatena las filas;
    //  el analisis posterior (tablas, graficos) se hace sobre ese CSV.
    //
    //  Si la auto-validacion fallo (ok=0) la fila debe DESCARTARSE: indica
    //  un bug, no un resultado.
    // ------------------------------------------------------------------
    if (csv_mode) {
        std::cout << in_path << ',' << algorithm_label << ',' << method
                  << ',' << iterations << ',' << perturb_strength
                  << ',' << accept_s << ',' << seed
                  << ',' << inc << ',' << sol.num_unassigned
                  << ',' << ms << ',' << ((ok_cost && ok_feas) ? 1 : 0) << '\n';
        try { write_solution(out_path, sol); } catch (...) {}
        return (ok_cost && ok_feas) ? 0 : 2;
    }

    std::cout << "\n---- Resultado: " << algorithm_label << " ----\n";
    std::cout << "  asignados          : " << (I.n - sol.num_unassigned) << " / " << I.n << "\n";
    std::cout << "  sin asignar        : " << sol.num_unassigned << "\n";
    std::cout << "  costo incremental  : " << inc << "\n";
    std::cout << "  costo recalculado  : " << fresh << "\n";
    std::cout << "  costo consistente  : " << (ok_cost ? "SI" : "NO  <-- BUG") << "\n";
    std::cout << "  factible           : " << (ok_feas ? "SI" : "NO  <-- BUG") << "\n";
    std::cout << "  tiempo algoritmo   : " << ms << " ms\n";

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

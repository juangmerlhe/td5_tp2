#ifndef GAP_SOLUTION_H
#define GAP_SOLUTION_H

// ===========================================================================
//  solution.h  --  Representacion de una solucion del GAP
// ===========================================================================
//
//  Representamos una solucion con un unico vector:
//      assign[j] = i   -> el vendedor j esta asignado al deposito i
//      assign[j] = UNASSIGNED (-1) -> el vendedor j NO esta asignado
//
//  Esta representacion ("cada vendedor apunta a su deposito") es la mas
//  comoda para el GAP porque la restriccion "cada vendedor en exactamente un
//  deposito" se cumple por construccion: un vendedor solo puede tener un valor
//  en assign[j]. Los conjuntos Gamma_i del enunciado se reconstruyen agrupando
//  los j que comparten el mismo i.
//
//  ----------------------------------------------------------------------------
//  PERFORMANCE: por que mantenemos load[] y current_cost incrementalmente
//  ----------------------------------------------------------------------------
//  Recalcular el costo total o la factibilidad desde cero cuesta O(n) o O(n+m).
//  En la busqueda local (Fase 2) vamos a evaluar MILLONES de movimientos, cada
//  uno cambiando una o dos asignaciones. Recalcular todo en cada movimiento
//  haria al programa miles de veces mas lento.
//
//  Solucion: mantenemos dos cantidades actualizadas en O(1) por movimiento:
//      load[i]      = capacidad usada del deposito i (suma de demandas alli)
//      current_cost = valor de la funcion objetivo de la solucion actual
//  Asi can_assign() y la diferencia de costo de un movimiento son O(1).
//
//  recompute_cost_from_scratch() existe SOLO para validar en modo debug que la
//  cuenta incremental nunca se desincronizo de la realidad.
// ===========================================================================

#include "instance.h"
#include <vector>
#include <cstdint>
#include <cmath>

struct Solution {
    static constexpr int UNASSIGNED = -1;

    const Instance* inst = nullptr;

    std::vector<int>     assign;  // assign[j] = deposito de j (o UNASSIGNED)
    std::vector<int64_t> load;    // load[i]   = capacidad usada del deposito i
    int    num_unassigned = 0;    // cantidad de vendedores sin asignar
    double current_cost   = 0.0;  // funcion objetivo (mantenida incrementalmente)

    // -----------------------------------------------------------------------
    //  Constructor: arranca con TODOS los vendedores sin asignar.
    //  El costo de esa solucion vacia es n * penalizacion.
    // -----------------------------------------------------------------------
    explicit Solution(const Instance& I)
        : inst(&I),
          assign(I.n, UNASSIGNED),
          load(I.m, 0),
          num_unassigned(I.n),
          current_cost(static_cast<double>(I.n) * I.penalty_per_unassigned) {}

    // -----------------------------------------------------------------------
    //  ¿Entra el vendedor j en el deposito i sin exceder su capacidad?
    //  O(1). Supone que j NO esta actualmente en i (caso tipico al asignar).
    // -----------------------------------------------------------------------
    bool can_assign(int j, int i) const {
        return load[i] + inst->demand[i][j] <= inst->capacity[i];
    }

    // -----------------------------------------------------------------------
    //  Asigna el vendedor j al deposito i. Precondicion: j estaba UNASSIGNED.
    //  Actualiza load, num_unassigned y current_cost en O(1).
    //  Nota: pagar el costo c_ij y dejar de pagar la penalizacion.
    // -----------------------------------------------------------------------
    void do_assign(int j, int i) {
        // (assign[j] deberia ser UNASSIGNED aqui)
        assign[j] = i;
        load[i]  += inst->demand[i][j];
        --num_unassigned;
        current_cost += inst->cost[i][j] - inst->penalty_per_unassigned;
    }

    // -----------------------------------------------------------------------
    //  Quita la asignacion del vendedor j (vuelve a UNASSIGNED). O(1).
    //  Deja de pagar c_ij y vuelve a pagar la penalizacion.
    // -----------------------------------------------------------------------
    void do_unassign(int j) {
        int i = assign[j];
        if (i == UNASSIGNED) return;
        assign[j] = UNASSIGNED;
        load[i]  -= inst->demand[i][j];
        ++num_unassigned;
        current_cost += inst->penalty_per_unassigned - inst->cost[i][j];
    }

    // -----------------------------------------------------------------------
    //  Mueve j de su deposito actual a i_new (operador SHIFT, util en Fase 2).
    //  Devuelve true si pudo (i_new tiene capacidad), false si no toca nada.
    // -----------------------------------------------------------------------
    bool move_to(int j, int i_new) {
        int i_old = assign[j];
        if (i_old == i_new) return false;

        // Si j estaba asignado, lo sacamos para liberar su carga antes de chequear.
        if (i_old != UNASSIGNED) load[i_old] -= inst->demand[i_old][j];
        bool fits = (load[i_new] + inst->demand[i_new][j] <= inst->capacity[i_new]);
        if (!fits) {
            // revertimos la liberacion: no hacemos el movimiento
            if (i_old != UNASSIGNED) load[i_old] += inst->demand[i_old][j];
            return false;
        }
        // aplicamos
        if (i_old != UNASSIGNED) {
            current_cost -= inst->cost[i_old][j];
        } else {
            current_cost -= inst->penalty_per_unassigned;
            --num_unassigned;
        }
        load[i_new]  += inst->demand[i_new][j];
        current_cost += inst->cost[i_new][j];
        assign[j] = i_new;
        return true;
    }

    // -----------------------------------------------------------------------
    //  Funcion objetivo actual (mantenida incrementalmente). O(1).
    // -----------------------------------------------------------------------
    double cost() const { return current_cost; }

    // -----------------------------------------------------------------------
    //  ¿Es factible? Ningun deposito excede su capacidad. O(m).
    //  (Si siempre usamos can_assign/do_assign, nunca deberia violarse, pero
    //   este chequeo sirve para validar.)
    // -----------------------------------------------------------------------
    bool is_feasible() const {
        for (int i = 0; i < inst->m; ++i)
            if (load[i] > inst->capacity[i]) return false;
        return true;
    }

    // -----------------------------------------------------------------------
    //  Recalcula el costo desde cero. SOLO para validacion/debug. O(n).
    // -----------------------------------------------------------------------
    double recompute_cost_from_scratch() const {
        double c = 0.0;
        int unassigned = 0;
        for (int j = 0; j < inst->n; ++j) {
            int i = assign[j];
            if (i == UNASSIGNED) ++unassigned;
            else c += inst->cost[i][j];
        }
        c += unassigned * inst->penalty_per_unassigned;
        return c;
    }
};

#endif // GAP_SOLUTION_H

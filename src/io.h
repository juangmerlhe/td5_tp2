#ifndef GAP_IO_H
#define GAP_IO_H

// ===========================================================================
//  io.h  --  Escritura de la solucion en el formato pedido por el enunciado
// ===========================================================================
//
//  Formato de salida (segun el enunciado):
//    - una linea por deposito; la linea i-esima corresponde al deposito i.
//    - en cada linea, los indices de los vendedores asignados a ese deposito,
//      separados por un espacio.
//
//  ----------------------------------------------------------------------------
//  OJO: BASE DE INDICES (0 o 1). EL ENUNCIADO NO LO ACLARA EXPLICITAMENTE.
//  ----------------------------------------------------------------------------
//  El enunciado modela N = {1, ..., n}, lo que sugiere indices que arrancan en
//  1. Pero internamente trabajamos con vectores que arrancan en 0. Dejamos la
//  base como UNA constante para poder cambiarla en un solo lugar. CONFIRMAR con
//  la catedra que esperan (0-based o 1-based) antes de la entrega.
// ===========================================================================

#include "instance.h"
#include "solution.h"
#include <fstream>
#include <stdexcept>
#include <string>

// Cambiar a 1 si la catedra espera indices base-1.
constexpr int OUTPUT_INDEX_BASE = 0;

inline void write_solution(const std::string& path, const Solution& sol) {
    const Instance& I = *sol.inst;

    // Agrupamos los vendedores por deposito.
    std::vector<std::vector<int>> by_depot(I.m);
    for (int j = 0; j < I.n; ++j) {
        int i = sol.assign[j];
        if (i != Solution::UNASSIGNED) by_depot[i].push_back(j);
    }

    std::ofstream out(path);
    if (!out) throw std::runtime_error("No se pudo escribir el archivo de salida: " + path);

    for (int i = 0; i < I.m; ++i) {
        for (size_t k = 0; k < by_depot[i].size(); ++k) {
            if (k) out << ' ';
            out << (by_depot[i][k] + OUTPUT_INDEX_BASE);
        }
        out << '\n';
    }
}

#endif // GAP_IO_H

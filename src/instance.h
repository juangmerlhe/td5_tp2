#ifndef GAP_INSTANCE_H
#define GAP_INSTANCE_H

// ===========================================================================
//  instance.h  --  Lectura y representacion de una instancia del GAP
// ===========================================================================
//
//  Una instancia del Generalized Assignment Problem (GAP) describe:
//    - m depositos 
//    - n vendedores 
//    - cost[i][j]   : costo de asignar el vendedor j al deposito i.
//                     En ThunderPack es la DISTANCIA que recorre j hasta i.
//    - demand[i][j] : unidades de capacidad que consume j si va al deposito i.
//                     En la instancia real demand[i][j] = demand[j] (no depende
//                     del deposito), pero guardamos la matriz completa para
//                     soportar la version general del GAP.
//    - capacity[i]  : capacidad maxima del deposito i.
//
//  FORMATO DEL ARCHIVO (igual para benchmark e instancia real):
//    Linea 1 : m n
//    Luego   : m*n valores  -> matriz de costos   (fila i = costos del dep. i)
//    Luego   : m*n valores  -> matriz de demandas (fila i = demandas del dep. i)
//    Luego   : m valores    -> capacidades
//
//  Los valores estan separados por espacios y saltos de linea cualquiera;
//  por eso leemos "token por token" con el operador >>, NO linea por linea.
//
//  IMPORTANTE: en las instancias benchmark todo es entero, pero en la
//  instancia real los COSTOS son decimales (distancias). Por eso cost es
//  double. Las demandas y capacidades son enteras (unidades = paquetes).
// ===========================================================================

#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <limits>
#include <cstdint>

struct Instance {
    int m = 0;  // cantidad de depositos
    int n = 0;  // cantidad de vendedores

    std::vector<std::vector<double>>  cost;     // cost[i][j]
    std::vector<std::vector<int64_t>> demand;   // demand[i][j]
    std::vector<int64_t>              capacity;  // capacity[i]

    // Valores derivados que usaremos en toda la resolucion:
    double cmax = 0.0;                  // max c_ij sobre todos los i, j
    double penalty_per_unassigned = 0.0; // 3 * cmax (penalizacion por vendedor sin asignar)

    // -----------------------------------------------------------------------
    //  Lee una instancia desde un archivo y devuelve el objeto Instance.
    //  Lanza std::runtime_error si el archivo no existe o el formato no cierra.
    // -----------------------------------------------------------------------
    static Instance read(const std::string& path) {
        std::ifstream in(path);
        if (!in) {
            throw std::runtime_error("No se pudo abrir el archivo de instancia: " + path);
        }

        // 1) Leemos TODOS los numeros del archivo a un vector plano.
        //    in >> x lee el proximo token numerico ignorando espacios/saltos.
        std::vector<double> tok;
        tok.reserve(1u << 20);
        double x;
        while (in >> x) tok.push_back(x);

        if (tok.size() < 2) {
            throw std::runtime_error("Instancia vacia o corrupta: " + path);
        }

        Instance I;
        I.m = static_cast<int>(tok[0]);
        I.n = static_cast<int>(tok[1]);

        // 2) Verificamos que la cantidad de numeros sea exactamente la esperada.
        const size_t expected = 2
                              + static_cast<size_t>(I.m) * I.n   // costos
                              + static_cast<size_t>(I.m) * I.n   // demandas
                              + static_cast<size_t>(I.m);        // capacidades
        if (tok.size() != expected) {
            throw std::runtime_error(
                "Formato inesperado en " + path +
                ": se leyeron " + std::to_string(tok.size()) +
                " numeros pero se esperaban " + std::to_string(expected) +
                " (m=" + std::to_string(I.m) + ", n=" + std::to_string(I.n) + ").");
        }

        // 3) Cargamos las tres secciones en orden.
        size_t p = 2; // puntero de lectura (ya consumimos m y n)

        I.cost.assign(I.m, std::vector<double>(I.n));
        for (int i = 0; i < I.m; ++i)
            for (int j = 0; j < I.n; ++j)
                I.cost[i][j] = tok[p++];

        I.demand.assign(I.m, std::vector<int64_t>(I.n));
        for (int i = 0; i < I.m; ++i)
            for (int j = 0; j < I.n; ++j)
                I.demand[i][j] = static_cast<int64_t>(tok[p++]);

        I.capacity.assign(I.m, 0);
        for (int i = 0; i < I.m; ++i)
            I.capacity[i] = static_cast<int64_t>(tok[p++]);

        // 4) Calculamos cmax y la penalizacion 3*cmax.
        I.cmax = 0.0;
        for (int i = 0; i < I.m; ++i)
            for (int j = 0; j < I.n; ++j)
                if (I.cost[i][j] > I.cmax) I.cmax = I.cost[i][j];
        I.penalty_per_unassigned = 3.0 * I.cmax;

        return I;
    }

    // -----------------------------------------------------------------------
    //  Estadisticas utiles para entender la instancia y debuggear.
    // -----------------------------------------------------------------------
    int64_t total_capacity() const {
        int64_t s = 0;
        for (int64_t c : capacity) s += c;
        return s;
    }

    // Suma de demandas "minimas" por vendedor (min sobre depositos).
    // Si esta suma supera la capacidad total, es IMPOSIBLE asignar a todos.
    int64_t min_total_demand() const {
        int64_t s = 0;
        for (int j = 0; j < n; ++j) {
            int64_t best = std::numeric_limits<int64_t>::max();
            for (int i = 0; i < m; ++i) best = std::min(best, demand[i][j]);
            s += best;
        }
        return s;
    }
};

#endif // GAP_INSTANCE_H

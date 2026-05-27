// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "circuitcore/field/GridSpec.h"

#include <cmath>
#include <stdexcept>

namespace circuitcore::field {

double cfl_dt(const GridSpec& g, double safety) {
    if (g.dx <= 0 || g.dy <= 0 || g.dz <= 0) {
        throw std::invalid_argument("cfl_dt: non-positive cell size");
    }
    const double inv2 = 1.0 / (g.dx * g.dx)
                      + 1.0 / (g.dy * g.dy)
                      + 1.0 / (g.dz * g.dz);
    return safety / (kC0 * std::sqrt(inv2));
}

}  // namespace circuitcore::field

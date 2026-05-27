// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/CrossSection.h"

namespace sikit::em2d {

double CrossSection::epsilon_r_at(double y, double z) const {
    (void)y;  // y doesn't matter — stack is laterally uniform
    if (z < 0.0) return 1.0;  // air above the board
    double z_top = 0.0;
    for (const auto& d : stack) {
        if (z >= z_top && z < z_top + d.thickness) {
            return d.epsilon_r;
        }
        z_top += d.thickness;
    }
    return 1.0;  // below the board: treat as air
}

}  // namespace sikit::em2d

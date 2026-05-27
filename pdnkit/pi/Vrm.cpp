// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "pi/Vrm.h"

namespace pdnkit::pi {

std::complex<double> vrm_impedance(const VrmModel& m, double omega) {
    return std::complex<double>(m.r_droop_ohm, omega * m.l_out_h);
}

}  // namespace pdnkit::pi

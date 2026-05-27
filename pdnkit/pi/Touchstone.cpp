// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "pi/Touchstone.h"

#include <format>
#include <fstream>
#include <sstream>

namespace pdnkit::pi {

bool write_touchstone_z1p(const std::filesystem::path& path,
                          const std::vector<TouchstoneSample>& samples,
                          const std::string& comment) {
    std::ofstream f(path);
    if (!f) return false;
    if (!comment.empty()) {
        // Each line in comment gets its own "! " prefix.
        std::istringstream is(comment);
        for (std::string line; std::getline(is, line); ) {
            f << "! " << line << "\n";
        }
    }
    f << "# Hz Z RI R 50\n";
    for (const auto& s : samples) {
        f << std::format("{:.6e}  {:.6e}  {:.6e}\n",
                          s.f_hz, s.z.real(), s.z.imag());
    }
    return f.good();
}

}  // namespace pdnkit::pi

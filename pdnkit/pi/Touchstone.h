// Touchstone v1 (.s1p) writer.
//
// Touchstone is the industry-standard exchange format for impedance and
// S-parameter data: every VNA, every commercial EM tool, ngspice, even
// HFSS read it. Writing the cavity Z(f) sweep as .s1p lets the user
// load it next to a measured trace and compare.
//
// Format we emit (Touchstone v1, impedance form):
//
//   ! optional comment lines
//   # Hz Z RI R 50
//   <f_hz>  <re(Z)>  <im(Z)>
//   ...
//
// Z (impedance) form is unambiguous and needs no reference-impedance
// conversion. Z0 in the option line (R 50) is documentation only --
// downstream tools can renormalize if needed.

#pragma once

#include <complex>
#include <filesystem>
#include <string>
#include <vector>

namespace pdnkit::pi {

struct TouchstoneSample {
    double f_hz = 0.0;
    std::complex<double> z{0.0, 0.0};
};

// Write a Z-form .s1p file. Returns true on success. Comment lines are
// prepended with "! " automatically; pass an empty string to skip.
bool write_touchstone_z1p(const std::filesystem::path& path,
                          const std::vector<TouchstoneSample>& samples,
                          const std::string& comment = "");

}  // namespace pdnkit::pi

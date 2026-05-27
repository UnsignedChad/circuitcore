// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Copper surface roughness models for conductor loss.
//
// Smooth-conductor skin-effect predicts loss that under-counts what real
// PCBs show above ~5 GHz, because the actual copper surface is rough at
// scales comparable to the skin depth. Two industry-standard correction
// factors apply to the surface resistance Rs:
//
//   * Hammerstad-Jensen (1980): simple closed form, accurate to ~10 GHz.
//       K = 1 + (2/pi) * atan(1.4 * (Delta/delta)^2)
//     where Delta is the RMS surface roughness height and delta is the
//     skin depth at the operating frequency.
//
//   * Huray "snowball" (2010): models the matte side of copper foil as
//       a layer of spheres on a flat plane; far more accurate than HJ
//       beyond ~20 GHz. Uses two fit parameters: sphere radius a_r and
//       a coverage ratio (effective area density of spheres).
//
// Foil-grade defaults that hardware engineers see on real BoMs:
//
//   STD / Standard:  ~3.0 um RMS (cheap consumer FR-4)
//   VLP   / RTF:     ~1.0 um RMS (mid-range, used most in HSD designs)
//   HVLP / SLP:      ~0.4 um RMS (PCIe Gen5+, 56 Gbps SerDes)
//
// Apply the factor by multiplying the smooth-copper Rs (or equivalently
// the conductor loss alpha_c) by the returned K value.

#pragma once

namespace sikit::analysis {

enum class RoughnessModel {
    None,                // smooth conductor (default)
    HammerstadJensen,    // simple, good to ~10 GHz
    Huray,               // better above 20 GHz
};

struct RoughnessSpec {
    RoughnessModel model = RoughnessModel::None;

    // Hammerstad-Jensen parameter.
    double rms_height = 1.0e-6;     // Delta, meters (1 um = VLP foil typical)

    // Huray "snowball" parameters. The standard fit interprets a layer of
    // 'sphere_density' spheres of radius 'sphere_radius' per unit area,
    // with a flat-coverage factor 'flat_coverage' (= 1 - density*pi*a^2).
    // Defaults below are the published Huray fit for RTF-grade copper.
    double sphere_radius   = 0.5e-6;   // meters
    double sphere_density  = 1.0e12;   // spheres per m^2
    double flat_coverage   = 0.5;      // dimensionless [0..1]
};

// Surface impedance correction factor K such that
//    Rs_rough = K * Rs_smooth
// frequency in Hz, sigma in S/m. Returns 1.0 if model is None or inputs
// are degenerate.
double roughness_factor(const RoughnessSpec& spec, double frequency_hz,
                         double sigma_copper);

// Skin depth at frequency f in copper of conductivity sigma. Exposed for
// tests and UI captions.
double skin_depth(double frequency_hz, double sigma_copper);

}  // namespace sikit::analysis

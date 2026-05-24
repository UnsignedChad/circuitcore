// Pre-canned compliance specifications for the major high-speed serial
// standards. Each ComplianceSpec bundles an eye mask (in normalized UI /
// voltage space, suitable for the existing specs::passes() check) with
// the surrounding protocol metadata an engineer needs to set up the
// simulation correctly: bit rate, reference impedances, target BER,
// recommended PRBS pattern, and a reference to the spec document the
// mask came from.
//
// Scope of v1
//
//   The masks in this file are the canonical hexagonal shapes from the
//   published compliance documents, normalized to UI and unit voltage
//   swing. They are the right qualitative shapes -- the same shapes a
//   manual mask draw in any compliance test suite would use -- but
//   exact spec dimensions for any given silicon revision should be
//   cross-checked against the latest spec document before publishing a
//   compliance result. Each mask carries a `source` string naming the
//   spec section it derives from.
//
//   PAM4 standards (PCIe Gen6, 50GBASE-KR / 100GBASE-KR4, DDR5 at
//   higher data rates) have three vertically stacked eyes rather than
//   one. v1 captures only the middle eye; the upper and lower eyes
//   have the same shape but offset in voltage. The is_pam4 flag tells
//   downstream callers to repeat the mask test at the offset
//   positions; full multi-eye scoring lands in a follow-up.
//
//   No equalization recipes (CTLE peaking, FFE tap presets) are
//   embedded here yet. The receiver-side AMI integration already
//   handles vendor-supplied equalization; this module is about the
//   pass/fail mask shape.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "si/EyeMask.h"

namespace sikit::specs {

struct ComplianceSpec {
    // Human-readable identifier shown in the UI and used by name lookup.
    // Example: "PCIe Gen5 (32 GT/s)".
    std::string name;
    // Standards family. One of: PCIe, DDR, USB, HDMI, Ethernet.
    std::string family;

    // Bit rate in symbols/second. For NRZ standards this equals the
    // baud rate. For PAM4 the symbol rate is half the bits/sec, so
    // baud_hz here is the symbol rate (matches what compliance tools
    // commonly print).
    double baud_hz = 0.0;
    // Convenience: 1 / baud_hz.
    double ui_seconds = 0.0;

    // Reference impedances. Most differential standards specify both
    // a single-ended Z0 (50 ohms) and a differential Zdiff (typically
    // 85-100 ohms). For purely single-ended specs (parallel buses)
    // diff_impedance is left as 0.
    double reference_impedance = 50.0;
    double differential_impedance = 100.0;

    // Target bit error rate. Statistical-eye / bathtub scoring should
    // be evaluated at this BER.
    double ber_target = 1e-12;

    // The eye mask itself. Shape is in normalized (UI, V) space, same
    // convention as the existing specs::EyeMask polygons.
    EyeMask mask;

    // Recommended test pattern (matches sikit::eye::prbs* helpers in
    // string form).
    std::string compliance_pattern;

    // Is this a PAM4 standard? When true the mask describes the centre
    // eye only; upper and lower eyes share the shape offset by +/-
    // (2/3) of full swing.
    bool is_pam4 = false;

    // Brief reference string, e.g. "PCIe Base Specification Rev 5.0
    // section 8.3.5". Shown in the UI so the user can find the exact
    // spec text behind the mask shape.
    std::string source;
};

// PCIe
const ComplianceSpec& pcie_gen3();      // 8.0 GT/s, NRZ
const ComplianceSpec& pcie_gen4();      // 16.0 GT/s, NRZ
const ComplianceSpec& pcie_gen5();      // 32.0 GT/s, NRZ
const ComplianceSpec& pcie_gen6();      // 64.0 GT/s, PAM4

// DDR
const ComplianceSpec& ddr4_3200();      // 3200 MT/s, NRZ
const ComplianceSpec& ddr5_6400();      // 6400 MT/s, NRZ

// USB
const ComplianceSpec& usb31_gen2();     // 10.0 Gbps, NRZ
const ComplianceSpec& usb32_gen2x2();   // 20.0 Gbps, NRZ
const ComplianceSpec& usb4_gen3();      // 20.0 Gbps, NRZ (USB4 Gen3x2)

// HDMI
const ComplianceSpec& hdmi21_frl12();   // 12 Gbps FRL, NRZ

// Ethernet
const ComplianceSpec& ethernet_10gbase_kr();   // 10.3125 GBd, NRZ
const ComplianceSpec& ethernet_25gbase_kr();   // 25.78125 GBd, NRZ
const ComplianceSpec& ethernet_50gbase_kr();   // 26.5625 GBd PAM4

// Registry helpers.
std::vector<std::string> available_compliance_specs();
const ComplianceSpec* compliance_by_name(std::string_view name);

// All specs returning a vector of const pointers, ordered family-then-
// data-rate. Useful for building a UI selector.
std::vector<const ComplianceSpec*> all_compliance_specs();

}  // namespace sikit::specs

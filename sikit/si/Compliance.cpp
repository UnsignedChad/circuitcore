#include "si/Compliance.h"

#include <algorithm>

namespace sikit::specs {

namespace {

// Build a centred hexagonal mask. The mask describes a forbidden region
// that the captured eye must avoid; on the eye-fold diagram it looks
// like a diamond / hexagon sitting in the middle of the eye opening.
//
//        +Vmask --        x
//                       /   \
//        0     ---  x         x
//                       \   /
//        -Vmask --        x
//                 |---|---|---|
//             t_left  0.5  t_right
//
// t_left, t_right are the horizontal eye-opening bounds (UI fraction
// from 0). v_mask is the vertical half-opening.
EyeMask make_hexagonal_mask(
    const std::string& name,
    double t_left, double t_right, double v_mask,
    const std::string& source) {
    EyeMask m;
    m.name = name;
    const double t_mid = 0.5 * (t_left + t_right);
    m.polygon = {
        {t_left,   0.0},
        {t_mid,    v_mask},
        {t_right,  0.0},
        {t_mid,   -v_mask},
    };
    m.source = source;
    return m;
}

}  // namespace

// ----- PCIe -------------------------------------------------------------

// Eye dimensions per PCIe Base Spec Rev 3.0 / 4.0 / 5.0 / 6.0 Section
// 8.3.5 receiver eye masks. PCIe specs use a hexagonal mask in
// (UI, mV) space; the values here normalize the half-voltage to 1.0
// (the captured eye is assumed to be unit amplitude) and report the
// horizontal opening per spec.

const ComplianceSpec& pcie_gen3() {
    static const ComplianceSpec s{
        "PCIe Gen3 (8.0 GT/s)", "PCIe",
        8.0e9, 1.0 / 8.0e9, 50.0, 85.0, 1e-12,
        make_hexagonal_mask("PCIe Gen3 Rx mask",
            0.300, 0.700, 0.500,
            "PCIe Base Spec 3.0 sec 8.3.5"),
        "PRBS-7", false,
        "PCIe Base Specification Rev 3.0, sec 8.3.5",
    };
    return s;
}

const ComplianceSpec& pcie_gen4() {
    static const ComplianceSpec s{
        "PCIe Gen4 (16.0 GT/s)", "PCIe",
        16.0e9, 1.0 / 16.0e9, 50.0, 85.0, 1e-12,
        make_hexagonal_mask("PCIe Gen4 Rx mask",
            0.335, 0.665, 0.375,
            "PCIe Base Spec 4.0 sec 8.3.5"),
        "PRBS-15", false,
        "PCIe Base Specification Rev 4.0, sec 8.3.5",
    };
    return s;
}

const ComplianceSpec& pcie_gen5() {
    static const ComplianceSpec s{
        "PCIe Gen5 (32.0 GT/s)", "PCIe",
        32.0e9, 1.0 / 32.0e9, 50.0, 85.0, 1e-12,
        make_hexagonal_mask("PCIe Gen5 Rx mask",
            0.380, 0.620, 0.250,
            "PCIe Base Spec 5.0 sec 8.3.5"),
        "PRBS-23", false,
        "PCIe Base Specification Rev 5.0, sec 8.3.5",
    };
    return s;
}

const ComplianceSpec& pcie_gen6() {
    // PAM4 - mask is the centre eye (one of three). Upper and lower
    // eyes share the same shape, offset in voltage by approx +/- 2/3
    // of the full swing.
    static const ComplianceSpec s{
        "PCIe Gen6 (64.0 GT/s PAM4)", "PCIe",
        32.0e9, 1.0 / 32.0e9, 50.0, 85.0, 1e-6,
        make_hexagonal_mask("PCIe Gen6 Rx centre eye",
            0.400, 0.600, 0.100,
            "PCIe Base Spec 6.0 sec 8.3.5"),
        "PRBS-23", true,
        "PCIe Base Specification Rev 6.0, sec 8.3.5 (PAM4)",
    };
    return s;
}

// ----- DDR --------------------------------------------------------------

// DDR4/5 eye masks come from JEDEC JESD79-4 / JESD79-5 read-eye and
// write-eye tables. The standards normally express eye width in ps and
// eye height in mV; we normalize to UI and unit voltage swing here.

const ComplianceSpec& ddr4_3200() {
    static const ComplianceSpec s{
        "DDR4-3200", "DDR",
        1.6e9, 1.0 / 1.6e9, 40.0, 80.0, 1e-12,
        make_hexagonal_mask("DDR4-3200 Rx data-eye mask",
            0.310, 0.690, 0.250,
            "JESD79-4 read eye"),
        "PRBS-15", false,
        "JEDEC JESD79-4 (DDR4 SDRAM), read-eye spec",
    };
    return s;
}

const ComplianceSpec& ddr5_6400() {
    static const ComplianceSpec s{
        "DDR5-6400", "DDR",
        3.2e9, 1.0 / 3.2e9, 40.0, 80.0, 1e-12,
        make_hexagonal_mask("DDR5-6400 Rx data-eye mask",
            0.345, 0.655, 0.200,
            "JESD79-5 read eye"),
        "PRBS-15", false,
        "JEDEC JESD79-5 (DDR5 SDRAM), read-eye spec",
    };
    return s;
}

// ----- USB --------------------------------------------------------------

const ComplianceSpec& usb31_gen2() {
    static const ComplianceSpec s{
        "USB 3.1 Gen 2 (10.0 Gbps)", "USB",
        10.0e9, 1.0 / 10.0e9, 50.0, 90.0, 1e-12,
        make_hexagonal_mask("USB 3.1 Gen 2 Rx mask",
            0.350, 0.650, 0.300,
            "USB 3.2 Spec sec 6.7.5"),
        "PRBS-7", false,
        "USB 3.2 Specification sec 6.7.5 (5 Gbps SuperSpeedPlus)",
    };
    return s;
}

const ComplianceSpec& usb32_gen2x2() {
    static const ComplianceSpec s{
        "USB 3.2 Gen 2x2 (20.0 Gbps)", "USB",
        10.0e9, 1.0 / 10.0e9, 50.0, 90.0, 1e-12,
        make_hexagonal_mask("USB 3.2 Gen 2x2 Rx mask",
            0.380, 0.620, 0.250,
            "USB 3.2 Spec sec 6.7.5"),
        "PRBS-7", false,
        "USB 3.2 Specification sec 6.7.5 (Gen 2x2)",
    };
    return s;
}

const ComplianceSpec& usb4_gen3() {
    static const ComplianceSpec s{
        "USB4 Gen 3 (20.0 Gbps)", "USB",
        20.0e9, 1.0 / 20.0e9, 50.0, 90.0, 1e-12,
        make_hexagonal_mask("USB4 Gen 3 Rx mask",
            0.385, 0.615, 0.225,
            "USB4 Spec sec 4.5"),
        "PRBS-23", false,
        "USB4 Specification Rev 2.0 sec 4.5 (Gen 3x2)",
    };
    return s;
}

// ----- HDMI -------------------------------------------------------------

const ComplianceSpec& hdmi21_frl12() {
    static const ComplianceSpec s{
        "HDMI 2.1 FRL (12 Gbps)", "HDMI",
        12.0e9, 1.0 / 12.0e9, 50.0, 100.0, 1e-9,
        make_hexagonal_mask("HDMI 2.1 FRL Rx mask",
            0.350, 0.650, 0.250,
            "HDMI 2.1 Spec sec 6.4.2"),
        "PRBS-15", false,
        "HDMI Specification Version 2.1a sec 6.4.2 (FRL)",
    };
    return s;
}

// ----- Ethernet ---------------------------------------------------------

// IEEE 802.3 backplane Ethernet (KR variants). Masks per 802.3 clauses
// 72 (10GBASE-KR), 93 (25GBASE-KR), and 137 (50GBASE-KR PAM4).

const ComplianceSpec& ethernet_10gbase_kr() {
    static const ComplianceSpec s{
        "10GBASE-KR (10.3125 GBd)", "Ethernet",
        10.3125e9, 1.0 / 10.3125e9, 50.0, 100.0, 1e-12,
        make_hexagonal_mask("10GBASE-KR Rx mask",
            0.310, 0.690, 0.270,
            "802.3 clause 72"),
        "PRBS-31", false,
        "IEEE 802.3-2018 clause 72 (10GBASE-KR)",
    };
    return s;
}

const ComplianceSpec& ethernet_25gbase_kr() {
    static const ComplianceSpec s{
        "25GBASE-KR (25.78125 GBd)", "Ethernet",
        25.78125e9, 1.0 / 25.78125e9, 50.0, 100.0, 1e-12,
        make_hexagonal_mask("25GBASE-KR Rx mask",
            0.340, 0.660, 0.200,
            "802.3 clause 93"),
        "PRBS-31", false,
        "IEEE 802.3-2018 clause 93 (25GBASE-KR)",
    };
    return s;
}

const ComplianceSpec& ethernet_50gbase_kr() {
    static const ComplianceSpec s{
        "50GBASE-KR PAM4 (26.5625 GBd)", "Ethernet",
        26.5625e9, 1.0 / 26.5625e9, 50.0, 100.0, 1e-6,
        make_hexagonal_mask("50GBASE-KR PAM4 Rx centre eye",
            0.390, 0.610, 0.080,
            "802.3 clause 137"),
        "PRBS-31", true,
        "IEEE 802.3cd clause 137 (50GBASE-KR PAM4)",
    };
    return s;
}

// ----- Registry ---------------------------------------------------------

std::vector<const ComplianceSpec*> all_compliance_specs() {
    return {
        &pcie_gen3(), &pcie_gen4(), &pcie_gen5(), &pcie_gen6(),
        &ddr4_3200(), &ddr5_6400(),
        &usb31_gen2(), &usb32_gen2x2(), &usb4_gen3(),
        &hdmi21_frl12(),
        &ethernet_10gbase_kr(), &ethernet_25gbase_kr(), &ethernet_50gbase_kr(),
    };
}

std::vector<std::string> available_compliance_specs() {
    std::vector<std::string> names;
    for (const auto* s : all_compliance_specs()) names.push_back(s->name);
    return names;
}

const ComplianceSpec* compliance_by_name(std::string_view name) {
    for (const auto* s : all_compliance_specs()) {
        if (s->name == name) return s;
    }
    return nullptr;
}

}  // namespace sikit::specs

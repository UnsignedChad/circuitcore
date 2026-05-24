// Touchstone → CSV exporter.
//
// Writes a per-frequency report of the key SI quantities the user would
// otherwise have to compute themselves: insertion loss in dB, return
// loss in dB, S21 phase, and the input impedance looking into port 1
// derived from S11 (Z_in = Z_ref · (1 + S11) / (1 − S11)).
//
// Output is plain CSV with a header row, no quoting, comma-separated.
// Loads cleanly into Excel, Python pandas, GNUmeric, etc. — none of
// the consumers need to parse Touchstone themselves.

#pragma once

#include <filesystem>
#include <string>

#include "si/Touchstone.h"

namespace sikit::touchstone {

class TouchstoneCsv {
public:
    // Returns the CSV body as an in-memory string. Caller can compare,
    // checksum, or stash it. Throws TouchstoneParseError if the file is
    // structurally inconsistent (port-count mismatch etc.).
    static std::string to_string(const TouchstoneFile& f);

    // Write to disk. Creates / truncates `path`. Throws if file can't
    // be opened.
    static void write_file(const TouchstoneFile& f,
                            const std::filesystem::path& path);
};

}  // namespace sikit::touchstone

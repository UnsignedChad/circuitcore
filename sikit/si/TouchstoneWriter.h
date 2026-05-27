// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Touchstone writer — emits a 2-port (or N-port) Touchstone .sNp file
// from an in-memory TouchstoneFile struct. Pairs with TouchstoneReader.
//
// Always writes in RI format with Hz scale to keep the output portable
// (every Touchstone-aware tool accepts that combination). Frequencies
// are stored already in Hz on the TouchstoneFile struct, so we don't
// need to know the original frequency unit to re-emit.

#pragma once

#include <filesystem>
#include <string>

#include "si/Touchstone.h"

namespace sikit::touchstone {

class TouchstoneWriter {
public:
    // Format the file as a Touchstone text body. Caller can compare,
    // hash, or embed the result. Throws TouchstoneParseError on
    // structural inconsistencies (port count mismatch, frequency count
    // mismatch, etc).
    static std::string to_string(const TouchstoneFile& f);

    // Write to disk. Creates / truncates `path`. Throws if the file
    // can't be opened.
    static void write_file(const TouchstoneFile& f,
                            const std::filesystem::path& path);
};

}  // namespace sikit::touchstone

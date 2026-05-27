// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Binary save/load for a (Field3D, Grid) pair.
//
// Format
// ------
//   Offset  Bytes  Meaning
//        0       4  magic 'MPFD'
//        4       4  version (uint32 LE, currently 1)
//        8       4  nx (int32 LE)
//       12       4  ny (int32 LE)
//       16       4  nz (int32 LE)
//       20       8  dx (double, IEEE 754 LE)
//       28       8  dy (double, IEEE 754 LE)
//       36       8  dz (double, IEEE 754 LE)
//       44       8  x0 (double)
//       52       8  y0 (double)
//       60       8  z0 (double)
//       68    8*N  field doubles in (i, j, k) order with i fastest
//
// 68-byte header + N*8 bytes of data. Sized to be hex-dumpable for
// debugging and to need zero parser machinery -- one read for the
// header, one read for the data.

#pragma once

#include <filesystem>
#include <utility>

#include "circuitcore/field/Field3D.h"
#include "mp/Grid.h"

namespace mpkit {

void save_field(const Grid& grid,
                const circuitcore::field::Field3D& f,
                const std::filesystem::path& path);

// Returns the loaded grid + field. Throws std::runtime_error on
// missing file, bad magic, or version mismatch.
std::pair<Grid, circuitcore::field::Field3D> load_field(
    const std::filesystem::path& path);

}  // namespace mpkit

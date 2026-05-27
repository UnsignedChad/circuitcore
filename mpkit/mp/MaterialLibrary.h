// Stock material catalogue for mpkit.
//
// Numbers are room-temperature handbook values rounded to reasonable
// precision -- engineering use, not metrology. CTE values are linear
// (1/K) for solids and meaningless for fluids (left NaN). Anisotropic
// composites (FR-4 in particular) report their dominant in-plane
// values; anyone needing axis-resolved properties supplies a custom
// Material instead.
//
// Sources roughly: NIST tables (copper, aluminium, silver), Rogers /
// IsolaGroup datasheets (FR-4 Tg130 family), engineeringtoolbox.com
// (air, common engineering data) where the NIST entries do not cover
// composite materials.

#pragma once

#include <string>
#include <vector>

#include "mp/Material.h"

namespace mpkit {

// Named lookup. Throws std::out_of_range if the name is not in the
// library. Names are case-insensitive and stable across releases (used
// as identifiers in saved project files).
Material material_by_name(const std::string& name);

// All names the library currently ships, sorted alphabetically. Useful
// for populating GUI dropdowns.
std::vector<std::string> material_names();

// Direct accessors for the substances mpkit reaches for most often.
// Provided for terse use in solver default configs.
Material copper();
Material fr4();
Material air();
Material aluminium();
Material silver();
Material solder_sac305();
Material polyimide();
Material iron();
Material chromel();
Material alumel();
Material constantan();
Material bismuth();

}  // namespace mpkit

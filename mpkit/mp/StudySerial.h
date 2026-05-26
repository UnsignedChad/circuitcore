// Round-trip mpkit Study to/from sexpr.
//
// The on-disk format is one .mpstudy file (sexpr) plus zero or more
// .mpfield binary sidecars referenced by StoredField. Saving creates
// the directory if needed and writes everything; loading reads the
// study tree and validates that referenced sidecars exist (callers
// load field data on-demand via FieldIO).

#pragma once

#include <filesystem>
#include <string>

#include "mp/Study.h"

namespace mpkit {

// Pure in-memory round-trip. Useful for tests and for embedding a
// study description in a CLI argument.
std::string                   study_to_sexpr(const Study& s);
Study                         study_from_sexpr(const std::string& src);

// File round-trip. dir is the study root directory; the model tree is
// written to dir/study.mpstudy and field sidecars are referenced by
// the relative paths already in StoredField::path.
//
// save_study creates dir if it does not exist; load_study throws
// std::runtime_error if dir/study.mpstudy is missing.
void  save_study(const Study& s, const std::filesystem::path& dir);
Study load_study(const std::filesystem::path& dir);

}  // namespace mpkit

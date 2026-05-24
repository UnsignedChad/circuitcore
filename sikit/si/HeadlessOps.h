// Headless command-line operations.
//
// Sikit ships as a Qt-driven GUI by default, but for CI integration,
// batch sweeps over stackup / trace-width / length, and regression
// testing of board files, the same analysis pipelines need to run
// from a script without instantiating QApplication. The functions in
// this header are the headless half of those pipelines: each one
// takes parsed inputs (Board, SiStackup, CLI parameters) and produces
// a printed result + exit code, never touching Qt.
//
// The CLI dispatch lives in main.cpp; the analysis logic that the
// dispatch calls into is here so it can be unit-tested separately
// from the argv parsing layer.

#pragma once

#include <filesystem>
#include <string>

#include "circuitcore/board/Board.h"
#include "si/SiStackup.h"

namespace sikit::cli {

// All helpers return a small process exit code by convention:
//   0  -- success
//   2  -- input parse / load failure
//   3  -- net not found
//   4  -- layer not found
//   5  -- analysis-side error (e.g. degenerate geometry)
//   6  -- output write failure
int impedance_op(const circuitcore::board::Board& board,
                 const sikit::si::SiStackup& sis,
                 const std::string& net_name,
                 const std::string& layer_name,
                 bool use_fdm);

int touchstone_op(const circuitcore::board::Board& board,
                  const sikit::si::SiStackup& sis,
                  const std::string& net_name,
                  const std::string& layer_name,
                  const std::filesystem::path& out_path,
                  double f_lo_hz, double f_hi_hz, int n_points,
                  bool use_fdm);

int spice_op(const circuitcore::board::Board& board,
             const sikit::si::SiStackup& sis,
             const std::string& net_name,
             const std::string& layer_name,
             const std::filesystem::path& out_path,
             int n_poles, double f_lo_hz, double f_hi_hz, int n_points,
             bool use_fdm);

int compliance_op(const std::filesystem::path& touchstone_in,
                  const std::string& spec_name);

int deembed_op(const std::filesystem::path& measured_in,
                const std::filesystem::path& fixture_in,
                const std::filesystem::path& out_path);

int compare_op(const std::filesystem::path& a,
                const std::filesystem::path& b,
                int s_param_index,
                double max_abs_db);

int skew_op(const circuitcore::board::Board& board,
             const sikit::si::SiStackup& sis,
             double budget_ps);

int list_specs_op();

int list_nets_op(const circuitcore::board::Board& board);

}  // namespace sikit::cli

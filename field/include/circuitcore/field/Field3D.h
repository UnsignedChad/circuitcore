// 3D scalar field on a uniform Cartesian grid.
//
// Storage is dense (nx*ny*nz doubles) in column-major-x order: i (x) is
// the fastest-varying index, then j (y), then k (z). This matches the
// memory order sikit's FDTD update kernels assume and lets mpkit's
// thermal / mechanical solvers reuse the same allocation without
// reshaping when handing fields across physics.
//
// All bounds checking is the caller's job -- this is a hot inner-loop
// type and at() is a single multiply-add.

#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace circuitcore::field {

class Field3D {
public:
    Field3D() = default;
    Field3D(int nx, int ny, int nz) { resize(nx, ny, nz); }

    void resize(int nx, int ny, int nz) {
        if (nx < 0 || ny < 0 || nz < 0) {
            throw std::invalid_argument("Field3D: negative extent");
        }
        nx_ = nx; ny_ = ny; nz_ = nz;
        data_.assign(static_cast<std::size_t>(nx) * ny * nz, 0.0);
    }

    int nx() const { return nx_; }
    int ny() const { return ny_; }
    int nz() const { return nz_; }
    std::size_t size() const { return data_.size(); }

    double& at(int i, int j, int k) {
        return data_[idx(i, j, k)];
    }
    double at(int i, int j, int k) const {
        return data_[idx(i, j, k)];
    }

    void fill(double v) { std::fill(data_.begin(), data_.end(), v); }

    // Raw access for kernels that want to walk the buffer linearly.
    double*       data()       { return data_.data(); }
    const double* data() const { return data_.data(); }

private:
    std::size_t idx(int i, int j, int k) const {
        return static_cast<std::size_t>(i)
             + static_cast<std::size_t>(j) * nx_
             + static_cast<std::size_t>(k) * nx_ * ny_;
    }
    int nx_ = 0, ny_ = 0, nz_ = 0;
    std::vector<double> data_;
};

}  // namespace circuitcore::field

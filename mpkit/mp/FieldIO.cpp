#include "mp/FieldIO.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace mpkit {

namespace {

constexpr char     kMagic[4] = {'M', 'P', 'F', 'D'};
constexpr uint32_t kVersion  = 1;

template <class T>
void write_le(std::ofstream& f, T v) {
    // Hosts circuitcore targets (x86_64 + arm64 macOS / linux + amd64
    // windows) are all LE so we just dump the raw bytes. If a BE host
    // shows up later, byte-swap here.
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <class T>
T read_le(std::ifstream& f) {
    T v{};
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    return v;
}

}  // namespace

void save_field(const Grid& grid,
                const circuitcore::field::Field3D& fld,
                const std::filesystem::path& path) {
    if (fld.size() !=
        static_cast<std::size_t>(grid.nx()) * grid.ny() * grid.nz()) {
        throw std::runtime_error(
            "mpkit::save_field: field shape does not match grid");
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        throw std::runtime_error(
            "mpkit::save_field: cannot open " + path.string());
    }
    f.write(kMagic, 4);
    write_le<uint32_t>(f, kVersion);
    write_le<int32_t>(f, grid.nx());
    write_le<int32_t>(f, grid.ny());
    write_le<int32_t>(f, grid.nz());
    write_le<double>(f, grid.dx());
    write_le<double>(f, grid.dy());
    write_le<double>(f, grid.dz());
    write_le<double>(f, grid.x0);
    write_le<double>(f, grid.y0);
    write_le<double>(f, grid.z0);
    f.write(reinterpret_cast<const char*>(fld.data()),
             static_cast<std::streamsize>(fld.size() * sizeof(double)));
    if (!f) {
        throw std::runtime_error(
            "mpkit::save_field: write failed on " + path.string());
    }
}

std::pair<Grid, circuitcore::field::Field3D> load_field(
    const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error(
            "mpkit::load_field: cannot open " + path.string());
    }
    char magic[4];
    f.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) {
        throw std::runtime_error(
            "mpkit::load_field: bad magic in " + path.string());
    }
    const uint32_t version = read_le<uint32_t>(f);
    if (version != kVersion) {
        throw std::runtime_error(
            "mpkit::load_field: unsupported version "
            + std::to_string(version) + " in " + path.string());
    }
    Grid g;
    g.spec.nx = read_le<int32_t>(f);
    g.spec.ny = read_le<int32_t>(f);
    g.spec.nz = read_le<int32_t>(f);
    g.spec.dx = read_le<double>(f);
    g.spec.dy = read_le<double>(f);
    g.spec.dz = read_le<double>(f);
    g.x0      = read_le<double>(f);
    g.y0      = read_le<double>(f);
    g.z0      = read_le<double>(f);

    circuitcore::field::Field3D fld(g.nx(), g.ny(), g.nz());
    f.read(reinterpret_cast<char*>(fld.data()),
            static_cast<std::streamsize>(fld.size() * sizeof(double)));
    if (!f) {
        throw std::runtime_error(
            "mpkit::load_field: short read on " + path.string());
    }
    return {g, fld};
}

}  // namespace mpkit

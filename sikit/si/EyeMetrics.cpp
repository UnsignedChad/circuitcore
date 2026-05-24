#include "si/EyeMetrics.h"

#include <algorithm>
#include <climits>

namespace sikit::eye {

namespace {

// Largest contiguous run of zero-count voltage bins at a fixed time
// column. Returns (lo_bin, hi_bin) where lo and hi span the empty
// run inclusively; both are -1 if no empty run exists.
std::pair<int, int> empty_v_band(const EyeGrid& g, int t_bin) {
    int best_lo = -1, best_hi = -1, best_len = 0;
    int cur_lo = -1, cur_len = 0;
    for (int v = 0; v < g.volt_bins; ++v) {
        if (g.at(t_bin, v) == 0) {
            if (cur_lo < 0) cur_lo = v;
            ++cur_len;
        } else {
            if (cur_len > best_len) {
                best_len = cur_len;
                best_lo = cur_lo;
                best_hi = v - 1;
            }
            cur_lo = -1;
            cur_len = 0;
        }
    }
    if (cur_len > best_len) {
        best_lo = cur_lo;
        best_hi = g.volt_bins - 1;
    }
    return {best_lo, best_hi};
}

}  // namespace

EyeMetrics measure_eye(const EyeGrid& g) {
    EyeMetrics m;
    if (g.time_bins <= 0 || g.volt_bins <= 0 || g.counts.empty() ||
        g.v_max <= g.v_min) {
        return m;
    }

    const double v_span = g.v_max - g.v_min;
    const double v_per_bin = v_span / g.volt_bins;
    const double v_mid = 0.5 * (g.v_max + g.v_min);
    m.v_threshold = v_mid;

    // Height: width of the largest empty voltage band at the centre time
    // column. We sweep ±1 column around centre and take the worst-case
    // (smallest) opening — that's the "minimum eye opening" the receiver
    // would actually sample.
    const int t_centre = g.time_bins / 2;
    int min_band_lo = -1, min_band_hi = -1;
    int min_len = INT_MAX;
    for (int dt = -1; dt <= 1; ++dt) {
        const int tc = std::clamp(t_centre + dt, 0, g.time_bins - 1);
        auto [lo, hi] = empty_v_band(g, tc);
        if (lo < 0) {                       // no empty band at this column
            min_band_lo = min_band_hi = -1;
            min_len = 0;
            break;
        }
        const int len = hi - lo + 1;
        if (len < min_len) {
            min_len = len;
            min_band_lo = lo;
            min_band_hi = hi;
        }
    }
    if (min_band_lo >= 0 && min_len > 0) {
        m.height_v = min_len * v_per_bin;
    }

    // Width: at the mid-voltage row (closest bin to v_mid), find the
    // largest empty time band — that's the horizontal eye opening.
    const int v_mid_bin = std::clamp(
        static_cast<int>((v_mid - g.v_min) / v_span * g.volt_bins),
        0, g.volt_bins - 1);

    int best_lo = -1, best_hi = -1, best_len = 0;
    int cur_lo = -1, cur_len = 0;
    for (int t = 0; t < g.time_bins; ++t) {
        if (g.at(t, v_mid_bin) == 0) {
            if (cur_lo < 0) cur_lo = t;
            ++cur_len;
        } else {
            if (cur_len > best_len) {
                best_len = cur_len;
                best_lo = cur_lo;
                best_hi = t - 1;
            }
            cur_lo = -1;
            cur_len = 0;
        }
    }
    if (cur_len > best_len) {
        best_len = cur_len;
        best_lo = cur_lo;
        best_hi = g.time_bins - 1;
    }
    if (best_len > 0) {
        m.width_ui = static_cast<double>(best_len) / g.time_bins;
    }

    // Jitter (peak-peak): width of the populated zero-crossing band at
    // the midpoint voltage. We use the time span between the leftmost
    // and rightmost non-zero bin OUTSIDE the eye-opening window we just
    // measured — i.e. the bin populations clustered around the
    // crossings. For v0 we approximate it as (time_bins - best_len) /
    // time_bins, which is the fraction of the UI populated by trace
    // crossings.
    if (best_len > 0 && best_len < g.time_bins) {
        m.jitter_pp_ui = static_cast<double>(g.time_bins - best_len) /
                          g.time_bins;
    }

    return m;
}

}  // namespace sikit::eye

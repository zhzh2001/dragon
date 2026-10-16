#include "game/erosion.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace game {

namespace {

constexpr int NEIGHBOURS = 8;
constexpr int DX[NEIGHBOURS] = {1, -1, 0, 0, 1, -1, 1, -1};
constexpr int DZ[NEIGHBOURS] = {0, 0, 1, -1, 1, 1, -1, -1};

// A cell's fixed jitter in [-1, 1]: an integer hash, the same everywhere.
float jitter(uint32_t i) {
    uint32_t z = i * 0x9E3779B9u + 0x7F4A7C15u;
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    z ^= z >> 16;
    return float(z >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

}  // namespace

ErosionResult erode(const std::vector<float>& heights, const std::vector<uint8_t>& fixed, int n, float cell,
                    const ErosionSettings& s, const std::vector<float>& hardness) {
    ErosionResult out;
    out.n = n;
    const size_t count = size_t(n) * size_t(n);
    out.heights = heights;
    out.flow.assign(count, 0.0f);
    out.deposit.assign(count, 0.0f);
    if (n < 3 || n > 65535 || heights.size() != count || fixed.size() != count) return out;

    std::vector<float>& h = out.heights;
    const float diagonal = cell * std::sqrt(2.0f);
    float distance[NEIGHBOURS];
    for (int k = 0; k < NEIGHBOURS; ++k) distance[k] = k < 4 ? cell : diagonal;
    // The base levels and every cell's column and row, once: worked out
    // from the index each time they were integer divisions, which the Pi
    // 1B's ARMv6 does in a library call -- most of a valley's 7 s erosion
    // there, at every valley of a run.
    std::vector<uint8_t> base(count);
    std::vector<uint16_t> column(count), row(count);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const size_t i = size_t(z) * size_t(n) + size_t(x);
            base[i] = fixed[i] != 0 || x == 0 || z == 0 || x == n - 1 || z == n - 1;
            column[i] = uint16_t(x);
            row[i] = uint16_t(z);
        }
    }
    auto is_base = [&](size_t i) { return base[i] != 0; };

    std::vector<uint32_t> receiver(count);
    std::vector<float> receiver_distance(count, cell);
    std::vector<uint32_t> donor_start(count + 1);
    std::vector<uint32_t> donors(count);
    std::vector<uint32_t> stack;
    stack.reserve(count);
    std::vector<uint32_t> pending;
    pending.reserve(1024);
    std::vector<float> area(count);
    std::vector<float> change(count);
    std::vector<float> noise(count);
    std::vector<float> route(count);
    std::vector<uint8_t> done(count);
    for (size_t i = 0; i < count; ++i) noise[i] = jitter(uint32_t(i)) * s.route_jitter;
    const bool hard = hardness.size() == count;
    const float cell_area = cell * cell;

    std::vector<float> power(count);
    for (int iteration = 0; iteration < s.iterations; ++iteration) {
        // The drainage changes slowly, so it is rebuilt every few passes: the
        // flood is most of the cost, and any tree of receivers keeps the
        // implicit step stable.
        if (iteration % std::max(s.route_every, 1) == 0) {
            // Route over the surface with its pits filled (Barnes' priority
            // flood, with a millimetre's rise a cell so the fill is never flat):
            // every cell then drains to a base level. Without it the ridged
            // noise's closed basins cut the drainage into fragments, the widest
            // catchment was a third of a square kilometre, and nothing but rills
            // formed. The heights themselves are not filled: a basin's cells get
            // a receiver above them, and the implicit step raises them toward it
            // -- the basin silts up while its rim is cut.
            {
                using Entry = std::pair<float, uint32_t>;  // height, then index: a fixed order
                std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
                std::fill(done.begin(), done.end(), uint8_t(0));
                for (size_t i = 0; i < count; ++i) {
                    if (is_base(i)) {
                        route[i] = h[i] + noise[i];
                        done[i] = 1;
                        open.push(Entry{route[i], uint32_t(i)});
                    }
                }
                while (!open.empty()) {
                    const Entry e = open.top();
                    open.pop();
                    const size_t c = e.second;
                    const int x = column[c], z = row[c];
                    for (int k = 0; k < NEIGHBOURS; ++k) {
                        const int nx = x + DX[k], nz = z + DZ[k];
                        if (nx < 0 || nz < 0 || nx >= n || nz >= n) continue;
                        const size_t j = size_t(nz) * size_t(n) + size_t(nx);
                        if (done[j]) continue;
                        done[j] = 1;
                        route[j] = std::max(h[j] + noise[j], route[c] + 0.001f);
                        open.push(Entry{route[j], uint32_t(j)});
                    }
                }
            }
            for (size_t i = 0; i < count; ++i) {
                receiver[i] = uint32_t(i);
                if (is_base(i)) continue;
                const int x = column[i], z = row[i];
                float best_slope = 0.0f;
                int best = -1;
                float lowest = 0.0f;
                int lowest_k = -1;
                for (int k = 0; k < NEIGHBOURS; ++k) {
                    const size_t j = size_t(z + DZ[k]) * size_t(n) + size_t(x + DX[k]);
                    const float hj = route[j];
                    const float slope = (route[i] - hj) / distance[k];
                    if (slope > best_slope) {
                        best_slope = slope;
                        best = k;
                    }
                    if (lowest_k < 0 || hj < lowest) {
                        lowest = hj;
                        lowest_k = k;
                    }
                }
                const int k = best >= 0 ? best : lowest_k;
                receiver[i] = uint32_t(size_t(z + DZ[k]) * size_t(n) + size_t(x + DX[k]));
                receiver_distance[i] = distance[k];
            }

            // Donors of each cell, in index order.
            std::fill(donor_start.begin(), donor_start.end(), 0u);
            for (size_t i = 0; i < count; ++i) {
                if (receiver[i] != i) ++donor_start[receiver[i] + 1];
            }
            for (size_t i = 0; i < count; ++i) donor_start[i + 1] += donor_start[i];
            {
                std::vector<uint32_t> fill(donor_start.begin(), donor_start.end() - 1);
                for (size_t i = 0; i < count; ++i) {
                    if (receiver[i] != i) donors[fill[receiver[i]]++] = uint32_t(i);
                }
            }

            // The stack: from every base level up its tree of donors, so each
            // cell comes after its receiver. Cells in a spill cycle are never
            // reached and stay as they are this pass.
            stack.clear();
            for (size_t b = 0; b < count; ++b) {
                if (receiver[b] != b) continue;
                pending.clear();
                pending.push_back(uint32_t(b));
                while (!pending.empty()) {
                    const uint32_t i = pending.back();
                    pending.pop_back();
                    stack.push_back(i);
                    for (uint32_t d = donor_start[i]; d < donor_start[i + 1]; ++d) pending.push_back(donors[d]);
                }
            }

            // Drainage area, summed from the tips down.
            for (size_t i = 0; i < count; ++i) area[i] = 1.0f;
            for (size_t k = stack.size(); k-- > 0;) {
                const uint32_t i = stack[k];
                if (receiver[i] != i) area[receiver[i]] += area[i];
            }
            // The stream power's area term, A^0.75 (in square metres) as three
            // square roots, which are exact on every compiler where powf is not.
            // Above sqrt(A), it favours the big catchments over the rills: the
            // valleys cut and the faces between them keep their shape, instead
            // of every slope combing into the same parallel grooves.
            for (size_t i = 0; i < count; ++i) {
                const float a = area[i] * cell_area;
                const float root = std::sqrt(a);
                power[i] = s.area_exponent_075 ? root * std::sqrt(root) : root;
            }
        }

        // The implicit stream-power step, receivers first.
        for (const uint32_t i : stack) {
            const uint32_t r = receiver[i];
            if (r == i || is_base(i)) continue;
            const float k = hard ? s.erodibility * hardness[i] : s.erodibility;
            const float f = k * power[i] / receiver_distance[i];
            h[i] = (h[i] + f * h[r]) / (1.0f + f);
        }

        // Talus: what stands steeper than the talus slope above a neighbour
        // slides a share of the excess onto it. Gathered, then applied, so
        // the order of the cells does not matter.
        for (int t = 0; t < s.thermal_iterations; ++t) {
            std::fill(change.begin(), change.end(), 0.0f);
            for (size_t i = 0; i < count; ++i) {
                if (is_base(i)) continue;
                const int x = column[i], z = row[i];
                for (int k = 0; k < NEIGHBOURS; ++k) {
                    const size_t j = size_t(z + DZ[k]) * size_t(n) + size_t(x + DX[k]);
                    const float excess = (h[i] - h[j]) - s.talus * distance[k];
                    if (excess <= 0.0f) continue;
                    // An eighth each way at most, so a cell never gives away
                    // more than its excess over all its neighbours together.
                    const float moved = excess * s.thermal_rate * 0.125f;
                    change[i] -= moved;
                    // The floor and the edge keep their height: what reaches
                    // them leaves the simulation.
                    if (!is_base(j)) change[j] += moved;
                }
            }
            for (size_t i = 0; i < count; ++i) {
                h[i] += change[i];
                if (change[i] > 0.0f) out.deposit[i] += change[i];
            }
        }
    }
    for (size_t i = 0; i < count; ++i) out.flow[i] = area[i];
    return out;
}

}  // namespace game

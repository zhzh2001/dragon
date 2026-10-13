#pragma once

// Erosion of the terrain's heightfield (docs/FIDELITY.md, F2): the shapes
// that make a mountain read as rock rather than clay, which no sum of noise
// octaves produces -- valleys that branch into gullies, the sharp spurs
// between them, and scree at the foot of the faces.
//
// Two processes, in turn:
//
//   - Fluvial: the stream power law, E = K sqrt(A) S, solved implicitly
//     (Braun and Willett 2013). Water runs from each cell to its steepest
//     lower neighbour; the area draining through a cell (A) is summed down
//     that tree; and each cell is lowered toward its receiver by an amount
//     that grows with sqrt(A), so the big catchments cut deep and the
//     single cells on a ridge hardly at all. That is what makes the network
//     dendritic. Implicit, so any step is stable.
//   - Thermal: wherever a slope is steeper than the talus angle, material
//     slides to the lower neighbours. It softens the cuts' walls into
//     slopes and lays the scree the materials show.
//
// A first version ran Beyer's droplets instead; on these slopes they left
// ripples across the flow rather than channels down it.
//
// Deterministic: no random numbers, a traversal fixed by the grid's order,
// and only IEEE-exact operations (sqrt, no libm), built without FMA
// contraction, so a seed erodes alike on every compiler (CLAUDE.md, "One
// random draw per statement").

#include <cstdint>
#include <vector>

namespace game {

struct ErosionSettings {
    // Fluvial passes, and the erodibility each applies (K dt, per metre of
    // sqrt(area)). Their product sets how deep the valleys cut.
    int iterations = 40;
    float erodibility = 0.002f;
    // The area exponent: 0.75 when set, else the textbook 0.5. And how
    // often the drainage is rebuilt, in passes.
    bool area_exponent_075 = true;
    int route_every = 4;
    // Thermal passes after each fluvial one, the talus slope (rise over
    // run) past which material slides, and how much of the excess moves.
    int thermal_iterations = 3;
    float talus = 0.9f;
    float thermal_rate = 0.25f;
    // Metres of fixed, per-cell noise the routing sees (and the heights do
    // not): on an even slope eight-direction routing runs every rill
    // parallel, like a curtain; jittered, the rills wander into each other
    // and merge, and the bigger channel then captures its neighbours.
    float route_jitter = 1.5f;
};

// The heightfield after erosion, and what the erosion did, on the same grid.
struct ErosionResult {
    int n = 0;                   // vertices per side
    std::vector<float> heights;  // metres, n * n, row-major (z rows, x columns)
    // The area that drains through each cell, in cells: stream beds and
    // gullies are its high values.
    std::vector<float> flow;
    // Metres the thermal passes laid down (net, so never negative): scree.
    std::vector<float> deposit;
};

// An eroded copy of `heights` (metres, n * n, row-major, `cell` metres
// apart). Cells where `fixed` is non-zero are base levels: water leaves the
// map through them and they are never changed (the valley floor, the edge).
// `hardness`, when not empty, scales the erodibility per cell (softer rock
// above 1 cuts more), so the gullies' density varies across a face.
ErosionResult erode(const std::vector<float>& heights, const std::vector<uint8_t>& fixed, int n, float cell,
                    const ErosionSettings& settings, const std::vector<float>& hardness = {});

}  // namespace game

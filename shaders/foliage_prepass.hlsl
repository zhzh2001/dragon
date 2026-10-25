// The crowns' depth, before anything else in the main pass
// (gfx::Foliage::draw_trees_prepass): the same vertex stage as foliage.hlsl,
// included whole so the two place every vertex alike to the bit, and a
// fragment stage that only cuts the cards out. The crowns are then shaded
// with an EQUAL depth test, once a pixel (foliage_shaded.hlsl). Alpha-tested cards defeat the
// tile GPU's hidden-surface removal, so every card behind the front one and
// the ground under the forest were lit in full: 2-3 ms of a 1080p frame.
#define FOLIAGE_PREPASS
#include "foliage.hlsl"

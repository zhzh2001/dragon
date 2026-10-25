// The crowns lit after their depth pre-pass (gfx::Foliage::draw_trees,
// foliage_prepass.hlsl): foliage.hlsl whole, without its cuts. An EQUAL
// depth test passes exactly the fragments the pre-pass kept, so the pre-pass
// alone decides which pixels a card covers; cut again here, the two shaders,
// compiled apart, disagreed at one card-edge pixel in a few thousand, and
// each was a hole of the clear colour.
#define FOLIAGE_AFTER_PREPASS
#include "foliage.hlsl"

#pragma once

#include "core/math.h"
#include "imgui.h"

namespace ui {

// The HUD kit: tokens, primitives and text, so every readout in the game is
// drawn from one vocabulary. Before it, the HUD was ImGui's bitmap font at
// several scales in boxes placed by pixel constants -- every element useful,
// none of them belonging to a system, and the whole reading as debug output
// with gold paint. The kit is what the playing layer and the panels share:
// open a panel and it looks like opening a drawer in the same cabinet.
//
// Layout is in FRAME UNITS: one unit is 1/720 of the frame height, so the
// same HUD survives 16:10, 4:3 and a retina window. Colours: one accent
// (gold), one danger (red, for threat and damage only), off-white text on
// translucent charcoal plates. Two faces: a condensed display face for
// numerals, a humanist sans for labels, both from TTF files into ImGui's
// atlas; ImGui's draw list is a fine renderer -- it was the bitmap font and
// the default style that read as debug.
struct Tokens {
    ImU32 plate = IM_COL32(18, 20, 24, 200);
    ImU32 plate_edge = IM_COL32(255, 235, 200, 28);
    ImU32 text = IM_COL32(238, 230, 214, 235);
    ImU32 text_dim = IM_COL32(238, 230, 214, 140);
    ImU32 accent = IM_COL32(226, 172, 60, 240);       // gold
    ImU32 accent_dim = IM_COL32(226, 172, 60, 120);
    ImU32 danger = IM_COL32(200, 60, 48, 235);        // red: threat and damage only
    ImU32 health = IM_COL32(196, 72, 60, 230);        // the mockup's dull red bar
    ImU32 breath = IM_COL32(226, 172, 60, 220);
    ImU32 breath_hot = IM_COL32(255, 150, 60, 240);
    ImU32 cool = IM_COL32(120, 180, 230, 230);        // boost, the one cool tone
    ImU32 ahead = IM_COL32(150, 220, 150, 240);       // a split gained
    ImU32 behind = IM_COL32(230, 120, 100, 240);      // a split lost
    ImU32 lock = IM_COL32(255, 215, 120, 245);
    ImU32 mark = IM_COL32(238, 230, 214, 140);        // an unremarkable target
    ImU32 flame = IM_COL32(255, 70, 30, 255);         // a rival holding its breath on you
};

enum class Align { Left, Centre, Right };

class Hud {
public:
    // Both may be null; the kit then draws with ImGui's default font.
    void set_fonts(ImFont* numeral, ImFont* label) {
        numeral_font_ = numeral;
        label_font_ = label;
    }
    // Once per frame, before any primitive.
    void begin(float width, float height);

    const Tokens& tokens() const { return tokens_; }
    Tokens& tokens() { return tokens_; }
    // Pixels for a length in frame units (1/720 of the height).
    float px(float units) const { return units * unit_; }
    float width() const { return width_; }
    float height() const { return height_; }
    // The safe margin from any edge, in pixels.
    float margin() const { return px(22.0f); }
    ImDrawList* draw() const { return draw_; }

    // ---- primitives ----
    void plate(ImVec2 min, ImVec2 max, float rounding_units = 6.0f) const;
    // A horizontal bar with an optional label to its left.
    void bar(ImVec2 pos, float width, float height, float fraction, ImU32 fill,
             const char* label = nullptr) const;
    // A stroked arc from `from` to `to` (radians), the filled part `fraction` of it.
    void arc(ImVec2 centre, float radius, float from, float to, float fraction, ImU32 colour,
             float thickness) const;
    // Four corner brackets around a point.
    void bracket(ImVec2 centre, float half, ImU32 colour, float thickness) const;
    // A round readiness pip: fills clockwise as `ready` goes 0..1, whole at 1.
    void pip(ImVec2 centre, float radius, float ready, const char* glyph, ImU32 colour) const;
    // An arrow at the edge of a circle about `centre`, pointing along `direction`.
    void edge_arrow(ImVec2 centre, core::Vec2 direction, float radius, ImU32 colour,
                    float size) const;
    // A short thick arc at the edge of a circle, for a threat direction.
    void edge_arc(ImVec2 centre, core::Vec2 direction, float radius, ImU32 colour,
                  float thickness, float half_width = 0.34f) const;

    // ---- text ----
    void label(ImVec2 pos, const char* text, ImU32 colour, float size_units = 15.0f,
               Align align = Align::Left) const;
    void numeral(ImVec2 pos, const char* text, ImU32 colour, float size_units = 32.0f,
                 Align align = Align::Left) const;
    float label_width(const char* text, float size_units = 15.0f) const;
    float numeral_width(const char* text, float size_units = 32.0f) const;

private:
    void text(ImFont* font, ImVec2 pos, const char* text, ImU32 colour, float size, Align align) const;
    float measure(ImFont* font, const char* text, float size) const;

    Tokens tokens_;
    ImFont* numeral_font_ = nullptr;
    ImFont* label_font_ = nullptr;
    ImDrawList* draw_ = nullptr;
    float width_ = 1280.0f;
    float height_ = 720.0f;
    float unit_ = 1.0f;
};

}  // namespace ui

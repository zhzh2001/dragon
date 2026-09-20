#include "ui/hud.h"

#include <cmath>

namespace ui {

void Hud::begin(float width, float height) {
    width_ = width;
    height_ = height;
    unit_ = height / 720.0f;
    draw_ = ImGui::GetForegroundDrawList();
}

void Hud::plate(ImVec2 min, ImVec2 max, float rounding_units) const {
    const float r = px(rounding_units);
    draw_->AddRectFilled(min, max, tokens_.plate, r);
    draw_->AddRect(min, max, tokens_.plate_edge, r);
}

void Hud::bar(ImVec2 pos, float width, float height, float fraction, ImU32 fill,
              const char* label_text) const {
    const float r = px(3.0f);
    draw_->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(0, 0, 0, 90), r);
    const float filled = width * core::saturate(fraction);
    if (filled > 0.5f) {
        draw_->AddRectFilled(pos, ImVec2(pos.x + filled, pos.y + height), fill, r);
    }
    draw_->AddRect(pos, ImVec2(pos.x + width, pos.y + height), tokens_.plate_edge, r);
    if (label_text) {
        label(ImVec2(pos.x, pos.y + height + px(2.0f)), label_text, tokens_.text_dim, 12.0f);
    }
}

void Hud::arc(ImVec2 centre, float radius, float from, float to, float fraction, ImU32 colour,
              float thickness) const {
    draw_->PathArcTo(centre, radius, from, to, 32);
    draw_->PathStroke(IM_COL32(0, 0, 0, 110), 0, thickness);
    const float f = core::saturate(fraction);
    if (f > 0.005f) {
        draw_->PathArcTo(centre, radius, from, from + (to - from) * f, 32);
        draw_->PathStroke(colour, 0, thickness);
    }
}

void Hud::bracket(ImVec2 centre, float half, ImU32 colour, float thickness) const {
    const float arm = half * 0.38f;
    const ImVec2 corners[4] = {ImVec2(centre.x - half, centre.y - half),
                               ImVec2(centre.x + half, centre.y - half),
                               ImVec2(centre.x + half, centre.y + half),
                               ImVec2(centre.x - half, centre.y + half)};
    const ImVec2 steps[4] = {ImVec2(arm, arm), ImVec2(-arm, arm), ImVec2(-arm, -arm),
                             ImVec2(arm, -arm)};
    for (int i = 0; i < 4; ++i) {
        draw_->AddLine(corners[i], ImVec2(corners[i].x + steps[i].x, corners[i].y), colour, thickness);
        draw_->AddLine(corners[i], ImVec2(corners[i].x, corners[i].y + steps[i].y), colour, thickness);
    }
}

void Hud::pip(ImVec2 centre, float radius, float ready, const char* glyph, ImU32 colour) const {
    draw_->AddCircleFilled(centre, radius, tokens_.plate, 24);
    const float inner = radius - px(3.0f);
    if (ready >= 1.0f) {
        draw_->AddCircleFilled(centre, inner, colour, 24);
    } else if (ready > 0.01f) {
        draw_->PathArcTo(centre, inner, -core::HALF_PI, -core::HALF_PI + core::TWO_PI * ready, 24);
        draw_->PathLineTo(centre);
        draw_->PathFillConvex((colour & 0x00FFFFFF) | (ImU32(110) << 24));
    }
    draw_->AddCircle(centre, radius, tokens_.plate_edge, 24);
    if (glyph) {
        const float size = radius * 1.05f;
        const float w = measure(label_font_, glyph, size);
        text(label_font_, ImVec2(centre.x - w * 0.5f, centre.y - size * 0.5f), glyph,
             ready >= 1.0f ? IM_COL32(20, 22, 26, 240) : tokens_.text, size, Align::Left);
    }
}

void Hud::edge_arrow(ImVec2 centre, core::Vec2 direction, float radius, ImU32 colour,
                     float size) const {
    const ImVec2 tip(centre.x + direction.x * radius, centre.y + direction.y * radius);
    const ImVec2 perpendicular(-direction.y, direction.x);
    draw_->AddTriangleFilled(
        tip,
        ImVec2(tip.x - direction.x * size * 2.0f + perpendicular.x * size,
               tip.y - direction.y * size * 2.0f + perpendicular.y * size),
        ImVec2(tip.x - direction.x * size * 2.0f - perpendicular.x * size,
               tip.y - direction.y * size * 2.0f - perpendicular.y * size),
        colour);
}

void Hud::edge_arc(ImVec2 centre, core::Vec2 direction, float radius, ImU32 colour,
                   float thickness, float half_width) const {
    const float angle = std::atan2(direction.y, direction.x);
    draw_->PathArcTo(centre, radius, angle - half_width, angle + half_width, 24);
    draw_->PathStroke(colour, 0, thickness);
}

float Hud::measure(ImFont* font, const char* string, float size) const {
    ImFont* f = font ? font : ImGui::GetFont();
    return f->CalcTextSizeA(size, FLT_MAX, 0.0f, string).x;
}

void Hud::text(ImFont* font, ImVec2 pos, const char* string, ImU32 colour, float size,
               Align align) const {
    ImFont* f = font ? font : ImGui::GetFont();
    float x = pos.x;
    if (align != Align::Left) {
        const float w = measure(f, string, size);
        x -= align == Align::Centre ? w * 0.5f : w;
    }
    draw_->AddText(f, size, ImVec2(x, pos.y), colour, string);
}

void Hud::label(ImVec2 pos, const char* string, ImU32 colour, float size_units, Align align) const {
    text(label_font_, pos, string, colour, px(size_units), align);
}

void Hud::numeral(ImVec2 pos, const char* string, ImU32 colour, float size_units, Align align) const {
    text(numeral_font_, pos, string, colour, px(size_units), align);
}

float Hud::label_width(const char* string, float size_units) const {
    return measure(label_font_, string, px(size_units));
}

float Hud::numeral_width(const char* string, float size_units) const {
    return measure(numeral_font_, string, px(size_units));
}

}  // namespace ui

#include "game/breath_matter.h"

#include <algorithm>
#include <cmath>

namespace game {

using core::Vec3;

namespace {

bool is_slug(MatterKind kind) { return kind == MatterKind::WaterSlug || kind == MatterKind::AcidSlug; }
bool is_drop(MatterKind kind) { return kind == MatterKind::WaterDrop || kind == MatterKind::AcidDrop; }

// Downward acceleration and air drag, by what the piece is. A lance flies
// flat; water and rock fall as they do.
float gravity_of(MatterKind kind) {
    switch (kind) {
        case MatterKind::IceLance: return -5.0f;
        case MatterKind::IceChip: return -18.0f;
        case MatterKind::Boulder: return -22.0f;
        case MatterKind::Rubble: return -22.0f;
        case MatterKind::WaterSlug: return -24.0f;
        case MatterKind::AcidSlug: return -22.0f;
        default: return -24.0f;
    }
}

float drag_of(MatterKind kind) {
    switch (kind) {
        // The primaries hardly slow: the throw is solved without drag
        // (solve_throw), and what drag there is is the shortfall.
        case MatterKind::IceLance: return 0.04f;
        case MatterKind::IceChip: return 1.2f;
        case MatterKind::Boulder: return 0.05f;
        case MatterKind::Rubble: return 0.4f;
        case MatterKind::WaterSlug:
        case MatterKind::AcidSlug: return 0.1f;
        default: return 1.0f;
    }
}

}  // namespace

float MatterMark::strength() const {
    const float in = core::saturate(age / 0.35f);
    const float out = core::saturate((life - age) / core::maxf(life * 0.2f, 0.01f));
    return core::minf(in, out);
}

float BreathMatter::unit01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) * (1.0f / 16777216.0f);
}

float BreathMatter::unit() { return unit01() * 2.0f - 1.0f; }

void BreathMatter::clear() {
    pieces_.clear();
    marks_.clear();
    impacts_.clear();
    for (float& owed : owed_) owed = 0.0f;
    drops_owed_ = 0.0f;
}

void BreathMatter::add(const MatterPiece& piece) {
    if (int(pieces_.size()) >= tuning.max_pieces) {
        // Full: what lies on the ground longest goes first. A chip or a drop
        // is not worth a piece in flight.
        size_t oldest = pieces_.size();
        float most = -1.0f;
        for (size_t i = 0; i < pieces_.size(); ++i) {
            if (!pieces_[i].resting) continue;
            if (pieces_[i].age > most) {
                most = pieces_[i].age;
                oldest = i;
            }
        }
        if (oldest == pieces_.size()) return;
        pieces_[oldest] = piece;
        return;
    }
    pieces_.push_back(piece);
}

Vec3 BreathMatter::solve_throw(Vec3 mouth, Vec3 direction, Vec3 carry, float reach, float speed, float gravity,
                               const MatterAim* aim) const {
    const Vec3 dir = core::normalize_or(direction, Vec3::forward());
    // Nothing aimed at: a point part of the way along the aim, moving with
    // the breather, so the arc crosses the aim there in the breather's own
    // frame.
    const Vec3 point = aim ? aim->point : mouth + dir * (core::maxf(reach, 10.0f) * core::saturate(tuning.aim_share));
    const Vec3 drift = aim ? aim->velocity : carry;
    speed = core::maxf(speed, 1.0f);
    float t = core::distance(point, mouth) / speed;
    Vec3 launch = dir * speed;
    for (int i = 0; i < 4; ++i) {
        t = core::clampf(t, 0.05f, 4.0f);
        // p(t) = mouth + (launch + carry) t + g t^2 / 2, and it must be the
        // point where the point will be.
        launch = (point + drift * t - mouth) * (1.0f / t) - carry - Vec3{0.0f, 0.5f * gravity * t, 0.0f};
        t *= core::length(launch) / speed;
    }
    const Vec3 thrown = core::normalize_or(launch, dir);
    // Out of reach, or behind: no throw meets it. Along the aim, then.
    return core::dot(thrown, dir) > 0.75f ? thrown : dir;
}

bool BreathMatter::predict(Element element, Vec3 mouth, Vec3 direction, Vec3 carry, float reach, const MatterAim* aim,
                           const std::function<float(float, float)>& ground, float water_level, Vec3& landing) const {
    MatterKind kind = MatterKind::IceLance;
    float speed = tuning.lance_speed;
    switch (element) {
        case Element::Frost: break;
        case Element::Stone: kind = MatterKind::Boulder, speed = tuning.boulder_speed; break;
        case Element::Tide: kind = MatterKind::WaterSlug, speed = tuning.water_speed; break;
        case Element::Blight: kind = MatterKind::AcidSlug, speed = tuning.acid_speed; break;
        default: return false;
    }
    const float gravity = gravity_of(kind);
    Vec3 velocity = solve_throw(mouth, direction, carry, reach, speed, gravity, aim) * speed + carry;
    Vec3 at = mouth;
    const float step = 1.0f / 30.0f;
    // A lance bursts in the air at the breath's reach.
    const float life = kind == MatterKind::IceLance ? core::maxf(reach, 10.0f) / speed * 1.1f : 5.0f;
    for (float t = 0.0f; t < life; t += step) {
        velocity.y += gravity * step;
        velocity = velocity * std::exp(-drag_of(kind) * step);
        at = at + velocity * step;
        const float floor = ground(at.x, at.z);
        if (at.y <= core::maxf(floor, water_level)) {
            landing = Vec3{at.x, core::maxf(floor, water_level), at.z};
            return true;
        }
    }
    landing = at;
    return kind == MatterKind::IceLance;
}

void BreathMatter::emit(Element element, Vec3 mouth, Vec3 direction, Vec3 carry, float reach, float dt,
                        const MatterAim* aim) {
    if (element != Element::Frost && element != Element::Stone && element != Element::Tide &&
        element != Element::Blight) {
        return;
    }
    const MatterTuning& t = tuning;
    const MatterKind primary = element == Element::Frost   ? MatterKind::IceLance
                               : element == Element::Stone ? MatterKind::Boulder
                               : element == Element::Tide  ? MatterKind::WaterSlug
                                                           : MatterKind::AcidSlug;
    const float throw_speed = element == Element::Frost   ? t.lance_speed
                              : element == Element::Stone ? t.boulder_speed
                              : element == Element::Tide  ? t.water_speed
                                                          : t.acid_speed;
    const Vec3 dir = solve_throw(mouth, direction, carry, reach, throw_speed, gravity_of(primary), aim);
    const Vec3 side = core::normalize_or(core::cross(dir, Vec3::up()), Vec3::right());
    const Vec3 lift = core::cross(side, dir);
    float rate = 0.0f, speed = 1.0f, spread = 0.0f;
    switch (element) {
        case Element::Frost: rate = t.lance_rate, speed = t.lance_speed, spread = t.lance_spread; break;
        case Element::Stone: rate = t.boulder_rate, speed = t.boulder_speed, spread = t.boulder_spread; break;
        // A liquid's slugs are the points its one stream is drawn through:
        // as many as the tube has nodes over a slug's life, and none thrown
        // wide.
        case Element::Tide: rate = float(kStreamNodes) / 1.7f, speed = t.water_speed; break;
        default: rate = float(kStreamNodes) / 1.9f, speed = t.acid_speed; break;
    }
    if (element == Element::Tide || element == Element::Blight) {
        stream_fed_ = true;
        stream_mouth_ = mouth;
    }
    float& owed = owed_[int(element)];
    owed += dt * rate;
    while (owed >= 1.0f) {
        owed -= 1.0f;
        // One draw a statement: the compilers order two in one differently.
        const float off_side = unit();
        const float off_lift = unit();
        const float pace = unit01();
        const float seed = unit01();
        const float girth = unit01();
        const float turn = unit();
        const float lead = unit01();
        const Vec3 thrown = core::normalize(dir + (side * off_side + lift * off_lift) * spread);
        MatterPiece p;
        p.seed = seed;
        p.share = 1.0f / core::maxf(rate, 1.0f);
        p.orientation = core::look_rotation(thrown, lift);
        switch (element) {
            case Element::Frost: {
                p.kind = MatterKind::IceLance;
                p.velocity = thrown * (speed * (0.9f + 0.2f * pace)) + carry;
                const float half = t.lance_length * (0.6f + 0.8f * seed);
                const float wide = half * 0.15f * (0.8f + 0.5f * girth);
                p.scale = Vec3{wide, wide, half};
                // End over end, about an axis across the flight: from
                // behind, its length shows for most of each turn.
                p.spin_axis = core::normalize_or(side * turn + lift * (girth - 0.5f), side);
                p.spin_rate = t.lance_tumble * (0.5f + pace);
                // It bursts where the breath's reach ends.
                p.life = core::maxf(reach, 10.0f) / speed * (1.0f + 0.25f * pace);
                break;
            }
            case Element::Stone: {
                p.kind = MatterKind::Boulder;
                p.velocity = thrown * (speed * (0.85f + 0.3f * pace)) + carry;
                const float r = t.boulder_size * (0.55f + 0.9f * seed);
                p.scale = Vec3{r * (0.85f + 0.3f * girth), r, r * (1.15f - 0.3f * girth)};
                p.spin_axis = core::normalize_or(side * turn + lift * (pace - 0.5f) + dir * 0.3f, side);
                p.spin_rate = 3.0f + 6.0f * girth;
                p.life = 4.5f;
                break;
            }
            default: {
                const bool water = element == Element::Tide;
                p.kind = water ? MatterKind::WaterSlug : MatterKind::AcidSlug;
                // All at one speed: they stay in the order they left in.
                p.velocity = thrown * speed + carry;
                const float wide = water ? t.water_width : t.acid_width;
                p.scale = Vec3{wide, wide, wide};
                p.life = water ? 1.7f : 1.9f;
                break;
            }
        }
        // Not all at the jaws at once: each has flown a part of this frame.
        p.position = mouth + (p.velocity - carry) * (lead * dt);
        add(p);
    }
}

void BreathMatter::mark(Element element, Vec3 ground, float radius, float life) {
    radius *= tuning.mark_size;
    for (MatterMark& m : marks_) {
        if (m.element != element) continue;
        if (core::distance(m.position, ground) > (m.radius + radius) * 0.6f) continue;
        // Renewed at full strength, and wider for each piece that lands.
        m.age = core::minf(m.age, 0.35f);
        m.life = core::maxf(m.life, life);
        m.radius = core::minf(m.radius + radius * 0.08f, radius * 2.2f);
        m.position = core::lerp(m.position, ground, 0.1f);
        return;
    }
    MatterMark m;
    m.position = ground;
    m.radius = radius;
    m.element = element;
    m.life = life;
    if (int(marks_.size()) < kMaxMarks) {
        marks_.push_back(m);
        return;
    }
    size_t oldest = 0;
    for (size_t i = 1; i < marks_.size(); ++i) {
        if (marks_[i].age / marks_[i].life > marks_[oldest].age / marks_[oldest].life) oldest = i;
    }
    marks_[oldest] = m;
}

void BreathMatter::spike(Vec3 ground, Vec3 lean, float size) {
    const float girth = unit01();
    const Vec3 axis = core::normalize_or(Vec3::up() + lean, Vec3::up());
    MatterPiece s;
    s.kind = MatterKind::IceSpike;
    s.resting = true;
    s.seed = girth;
    s.orientation = core::look_rotation(axis, std::fabs(axis.y) > 0.9f ? Vec3::right() : Vec3::up());
    s.scale = Vec3{size * (0.16f + 0.1f * girth), size * (0.16f + 0.1f * girth), size};
    // Its lower third is in the ground.
    s.position = ground + axis * (size * 0.35f);
    s.life = tuning.spike_life * (0.7f + 0.6f * girth);
    add(s);
}

void BreathMatter::land(const MatterPiece& piece, Vec3 at, float ground, bool on_water, bool on_target,
                        bool in_air, int target) {
    const bool on_ground = !on_water && !on_target && !in_air;
    const Vec3 down_range = core::normalize_or(piece.velocity, Vec3::forward());
    const float size = core::maxf(piece.scale.x, core::maxf(piece.scale.y, piece.scale.z));
    MatterImpact impact;
    impact.kind = piece.kind;
    impact.position = at;
    impact.velocity = piece.velocity;
    impact.size = size;
    impact.on_water = on_water;
    impact.on_target = on_target;
    impact.in_air = in_air;
    impact.target = target;
    impact.share = piece.share;
    impacts_.push_back(impact);

    // Off the ground a piece's speed turns upward and most of it is lost.
    Vec3 bounce = piece.velocity;
    if (on_ground || on_water) bounce.y = std::fabs(bounce.y);
    const Vec3 ground_point{at.x, ground, at.z};

    switch (piece.kind) {
        case MatterKind::IceLance: {
            const int chips = in_air ? 7 : 5;
            for (int i = 0; i < chips; ++i) {
                const float x = unit();
                const float y = unit();
                const float z = unit();
                const float pace = unit01();
                const float seed = unit01();
                MatterPiece c;
                c.kind = MatterKind::IceChip;
                c.seed = seed;
                c.position = at + Vec3{x, std::fabs(y), z} * 0.3f;
                c.velocity = bounce * (in_air ? 0.45f : 0.16f) + Vec3{x, y + (in_air ? 0.0f : 0.9f), z} * (5.0f + 7.0f * pace);
                const float half = piece.scale.z * (0.16f + 0.2f * seed);
                c.scale = Vec3{half * 0.3f, half * 0.3f, half};
                c.orientation = core::look_rotation(core::normalize_or(Vec3{x, y, z}, Vec3::up()));
                c.spin_axis = core::normalize_or(Vec3{z, x, y}, Vec3::up());
                c.spin_rate = 8.0f + 14.0f * pace;
                c.life = 0.55f + 0.6f * pace;
                add(c);
            }
            if (on_ground) {
                mark(Element::Frost, ground_point, 2.2f, tuning.mark_life);
                if (unit01() < tuning.spike_share) {
                    const float x = unit();
                    const float z = unit();
                    const float tall = unit01();
                    // It leans back the way it came.
                    spike(ground_point, Vec3{-down_range.x * 0.55f + x * 0.25f, 0.0f, -down_range.z * 0.55f + z * 0.25f},
                          piece.scale.z * (0.7f + 0.6f * tall));
                }
            }
            break;
        }
        case MatterKind::Boulder: {
            if (on_water) break;
            const int pieces = on_target ? 4 : 5;
            for (int i = 0; i < pieces; ++i) {
                const float x = unit();
                const float y = unit01();
                const float z = unit();
                const float pace = unit01();
                const float seed = unit01();
                MatterPiece r;
                r.kind = MatterKind::Rubble;
                r.seed = seed;
                const float radius = size * (0.25f + 0.25f * seed);
                r.position = at + Vec3{x * 0.4f, radius + 0.1f, z * 0.4f};
                r.velocity = bounce * 0.22f + Vec3{x, 0.5f + y, z} * (3.0f + 6.0f * pace);
                r.scale = Vec3{radius * (0.8f + 0.4f * pace), radius, radius * (1.2f - 0.4f * pace)};
                r.orientation = core::look_rotation(core::normalize_or(Vec3{x, y - 0.5f, z}, Vec3::up()));
                r.spin_axis = core::normalize_or(Vec3{z, y, -x}, Vec3::up());
                r.spin_rate = 4.0f + 8.0f * pace;
                r.life = 5.0f;
                add(r);
            }
            if (on_ground) {
                mark(Element::Stone, ground_point, 2.6f, tuning.mark_life);
                if (unit01() < 0.4f) {
                    // What is left of it, half in the ground.
                    MatterPiece rest = piece;
                    rest.kind = MatterKind::Rubble;
                    rest.resting = true;
                    rest.velocity = Vec3::zero();
                    rest.scale = piece.scale * 0.8f;
                    rest.position = ground_point + Vec3{0.0f, piece.scale.y * 0.25f, 0.0f};
                    rest.age = 0.0f;
                    rest.life = tuning.rubble_life;
                    add(rest);
                }
            }
            break;
        }
        case MatterKind::WaterSlug:
        case MatterKind::AcidSlug: {
            const bool water = piece.kind == MatterKind::WaterSlug;
            if (on_ground) mark(water ? Element::Tide : Element::Blight, ground_point, water ? 2.6f : 2.1f, tuning.mark_life);
            const int drops = on_ground || on_target ? (water ? 3 : 2) : 0;
            for (int i = 0; i < drops; ++i) {
                const float x = unit();
                const float y = unit01();
                const float z = unit();
                const float pace = unit01();
                MatterPiece d;
                d.kind = water ? MatterKind::WaterDrop : MatterKind::AcidDrop;
                d.seed = pace;
                d.position = at + Vec3{x * 0.3f, 0.15f, z * 0.3f};
                d.velocity = Vec3{bounce.x, 0.0f, bounce.z} * 0.15f + Vec3{x * 5.0f, 4.0f + 6.0f * y, z * 5.0f};
                const float r = 0.05f + 0.08f * pace;
                d.scale = Vec3{r, r, r * 1.6f};
                d.life = 0.5f + 0.5f * pace;
                add(d);
            }
            break;
        }
        default: break;
    }
}

void BreathMatter::update(float dt, const std::function<float(float, float)>& ground, float water_level,
                          const std::vector<Target>& targets) {
    impacts_.clear();
    stream_live_ = stream_fed_;
    stream_fed_ = false;
    for (MatterMark& m : marks_) m.age += dt;
    marks_.erase(std::remove_if(marks_.begin(), marks_.end(), [](const MatterMark& m) { return m.age >= m.life; }),
                 marks_.end());

    // Drops leave the stream along its length.
    {
        int slugs = 0;
        float rate = 0.0f;
        for (const MatterPiece& p : pieces_) {
            if (!is_slug(p.kind)) continue;
            ++slugs;
            rate = p.kind == MatterKind::WaterSlug ? tuning.water_drops : tuning.acid_drops;
        }
        if (slugs == 0) {
            drops_owed_ = 0.0f;
        } else {
            drops_owed_ += dt * rate * core::minf(1.0f, float(slugs) / float(kStreamNodes));
            const size_t count = pieces_.size();
            while (drops_owed_ >= 1.0f) {
                drops_owed_ -= 1.0f;
                const float pick = unit01();
                const float x = unit();
                const float y = unit();
                const float z = unit();
                const float pace = unit01();
                // The slug at that place in the list.
                int want = int(pick * float(slugs));
                const MatterPiece* from = nullptr;
                for (size_t i = 0; i < count; ++i) {
                    if (!is_slug(pieces_[i].kind)) continue;
                    if (want-- == 0) {
                        from = &pieces_[i];
                        break;
                    }
                }
                if (!from) break;
                MatterPiece d;
                d.kind = from->kind == MatterKind::WaterSlug ? MatterKind::WaterDrop : MatterKind::AcidDrop;
                d.seed = pace;
                const float wide = from->scale.x * (1.0f + 1.5f * from->age / from->life);
                // Anywhere along the stream, not only at its nodes: up to
                // half a node's gap before or behind this one.
                const float gap = from->life / float(kStreamNodes);
                d.position = from->position + Vec3{x, y, z} * wide + from->velocity * ((pick * 7.0f - std::floor(pick * 7.0f) - 0.5f) * gap);
                d.velocity = from->velocity * (0.8f + 0.15f * pace) + Vec3{x, y, z} * (2.5f + 4.5f * pace);
                const float r = from->scale.x * (0.1f + 0.22f * pace);
                d.scale = Vec3{r, r, r * 1.8f};
                d.life = 0.5f + 0.5f * pace;
                add(d);  // may move the list: `from` is not used after it
            }
        }
    }

    const size_t count = pieces_.size();
    for (size_t i = 0; i < count; ++i) {
        MatterPiece p = pieces_[i];  // a copy: land() adds to the list
        p.age += dt;
        if (p.resting) {
            pieces_[i] = p;
            continue;
        }
        p.velocity.y += gravity_of(p.kind) * dt;
        p.velocity = p.velocity * std::exp(-drag_of(p.kind) * dt);
        p.position = p.position + p.velocity * dt;
        if (is_slug(p.kind) || is_drop(p.kind)) {
            p.orientation = core::look_rotation(core::normalize_or(p.velocity, Vec3::forward()));
        } else {
            p.orientation = core::integrate(p.orientation, p.spin_axis * p.spin_rate, dt);
        }

        const float floor = ground(p.position.x, p.position.z);
        const float reach = core::minf(p.scale.x, p.scale.y);
        const bool primary = p.kind == MatterKind::IceLance || p.kind == MatterKind::Boulder || is_slug(p.kind);
        bool gone = false;
        if (floor < water_level && p.position.y <= water_level) {
            if (primary) land(p, Vec3{p.position.x, water_level, p.position.z}, water_level, true, false, false);
            gone = true;
        } else if (p.position.y - reach <= floor) {
            if (p.kind == MatterKind::Rubble) {
                const float speed = core::length(p.velocity);
                if (speed > 6.0f) {
                    // One more hop, most of it lost.
                    p.velocity = Vec3{p.velocity.x * 0.55f, std::fabs(p.velocity.y) * 0.3f, p.velocity.z * 0.55f};
                    p.position.y = floor + reach;
                    p.spin_rate *= 0.5f;
                } else {
                    p.resting = true;
                    p.velocity = Vec3::zero();
                    p.position.y = floor + p.scale.y * 0.5f;
                    p.age = 0.0f;
                    p.life = tuning.rubble_life * (0.7f + 0.6f * p.seed);
                }
            } else {
                if (primary) land(p, p.position, floor, false, false, false);
                gone = true;
            }
        } else if (primary) {
            for (const Target& target : targets) {
                if (core::distance(target.position, p.position) > target.radius + reach) continue;
                land(p, p.position, floor, false, true, false, target.id);
                gone = true;
                break;
            }
        }
        if (!gone && p.age >= p.life) {
            if (p.kind == MatterKind::IceLance) land(p, p.position, floor, false, false, true);
            gone = true;
        }
        if (gone) p.life = -1.0f;
        pieces_[i] = p;
    }
    pieces_.erase(std::remove_if(pieces_.begin(), pieces_.end(),
                                 [](const MatterPiece& p) { return p.life < 0.0f || p.age >= p.life; }),
                  pieces_.end());
}

}  // namespace game

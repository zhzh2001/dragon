#include "anim/model_pack.h"

#include <SDL3/SDL_filesystem.h>

#include <cstdio>
#include <cstring>

#include "anim/gltf_loader.h"
#include "anim/skin_lod.h"
#include "core/log.h"

namespace anim {
namespace {

// The file: a header, then each section as the writer below spells it, all
// little-endian, every field written on its own (no struct is dumped raw, so
// the layout does not depend on a compiler's padding). VERSION changes when
// the layout does; a pack from another version is refused, and the loader
// falls back to the glTF.
constexpr char MAGIC[8] = {'D', 'R', 'G', 'N', 'P', 'A', 'C', 'K'};
// 2: the low LODs are vertex-coloured (anim/skin_lod.h); a version-1 pack's
// textured ones scramble their atlas, so it is refused and rebuilt.
constexpr uint32_t VERSION = 2;

class Writer {
public:
    void u8(uint8_t v) { buf_.push_back(v); }
    void u32(uint32_t v) { raw(&v, 4); }
    void i32(int32_t v) { raw(&v, 4); }
    void u64(uint64_t v) { raw(&v, 8); }
    void i64(int64_t v) { raw(&v, 8); }
    void f32(float v) { raw(&v, 4); }
    void str(const std::string& s) {
        u32(uint32_t(s.size()));
        raw(s.data(), s.size());
    }
    void bytes(const std::vector<uint8_t>& b) {
        u32(uint32_t(b.size()));
        raw(b.data(), b.size());
    }
    void vec2(const core::Vec2& v) { f32(v.x); f32(v.y); }
    void vec3(const core::Vec3& v) { f32(v.x); f32(v.y); f32(v.z); }
    void vec4(const core::Vec4& v) { f32(v.x); f32(v.y); f32(v.z); f32(v.w); }
    void quat(const core::Quat& q) { f32(q.x); f32(q.y); f32(q.z); f32(q.w); }
    void mat4(const core::Mat4& m) { for (const core::Vec4& c : m.col) vec4(c); }
    void raw(const void* data, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + size);
    }
    const std::vector<uint8_t>& data() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

class Reader {
public:
    explicit Reader(std::vector<uint8_t> data) : buf_(std::move(data)) {}
    bool ok() const { return ok_; }
    bool at_end() const { return pos_ == buf_.size(); }
    uint8_t u8() { uint8_t v = 0; raw(&v, 1); return v; }
    uint16_t u16() { uint16_t v = 0; raw(&v, 2); return v; }
    uint32_t u32() { uint32_t v = 0; raw(&v, 4); return v; }
    int32_t i32() { int32_t v = 0; raw(&v, 4); return v; }
    uint64_t u64() { uint64_t v = 0; raw(&v, 8); return v; }
    int64_t i64() { int64_t v = 0; raw(&v, 8); return v; }
    float f32() { float v = 0; raw(&v, 4); return v; }
    std::string str() {
        const uint32_t n = u32();
        if (!fits(n)) return {};
        std::string s(reinterpret_cast<const char*>(&buf_[pos_]), n);
        pos_ += n;
        return s;
    }
    std::vector<uint8_t> bytes() {
        const uint32_t n = u32();
        if (!fits(n)) return {};
        std::vector<uint8_t> b(buf_.begin() + long(pos_), buf_.begin() + long(pos_ + n));
        pos_ += n;
        return b;
    }
    core::Vec2 vec2() { core::Vec2 v; v.x = f32(); v.y = f32(); return v; }
    core::Vec3 vec3() { core::Vec3 v; v.x = f32(); v.y = f32(); v.z = f32(); return v; }
    core::Vec4 vec4() { core::Vec4 v; v.x = f32(); v.y = f32(); v.z = f32(); v.w = f32(); return v; }
    core::Quat quat() { core::Quat q; q.x = f32(); q.y = f32(); q.z = f32(); q.w = f32(); return q; }
    core::Mat4 mat4() { core::Mat4 m; for (core::Vec4& c : m.col) c = vec4(); return m; }
    // A count that must leave room for `each` bytes per element.
    uint32_t count(size_t each) {
        const uint32_t n = u32();
        if (!fits(uint64_t(n) * each)) return 0;
        return n;
    }

private:
    bool fits(uint64_t n) {
        if (!ok_ || n > buf_.size() - pos_) {
            ok_ = false;
            return false;
        }
        return true;
    }
    void raw(void* out, size_t size) {
        if (!fits(size)) {
            std::memset(out, 0, size);
            return;
        }
        std::memcpy(out, &buf_[pos_], size);
        pos_ += size;
    }
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
    bool ok_ = true;
};

void write_mesh(Writer& w, const SkinnedMeshData& mesh) {
    w.u32(uint32_t(mesh.vertices.size()));
    for (const SkinnedVertex& v : mesh.vertices) {
        w.vec3(v.position);
        w.vec3(v.normal);
        w.vec3(v.color);
        w.vec2(v.uv);
        w.vec4(v.tangent);
        w.raw(v.joints, 4);
        for (float weight : v.weights) w.f32(weight);
    }
    w.u32(uint32_t(mesh.indices.size()));
    w.raw(mesh.indices.data(), mesh.indices.size() * 4);
    w.u32(uint32_t(mesh.submeshes.size()));
    for (const SkinnedSubmesh& s : mesh.submeshes) {
        w.u32(s.index_offset);
        w.u32(s.index_count);
        w.i32(s.base_color_texture);
        w.i32(s.normal_texture);
        w.i32(s.orm_texture);
        w.u32(uint32_t(s.palette.size()));
        w.raw(s.palette.data(), s.palette.size() * 2);
    }
}

void read_mesh(Reader& r, SkinnedMeshData& mesh) {
    constexpr size_t VERTEX_BYTES = 12 + 12 + 12 + 8 + 16 + 4 + 16;
    mesh.vertices.resize(r.count(VERTEX_BYTES));
    for (SkinnedVertex& v : mesh.vertices) {
        v.position = r.vec3();
        v.normal = r.vec3();
        v.color = r.vec3();
        v.uv = r.vec2();
        v.tangent = r.vec4();
        for (uint8_t& j : v.joints) j = r.u8();
        for (float& weight : v.weights) weight = r.f32();
    }
    mesh.indices.resize(r.count(4));
    for (uint32_t& i : mesh.indices) i = r.u32();
    mesh.submeshes.resize(r.count(6 * 4));
    for (SkinnedSubmesh& s : mesh.submeshes) {
        s.index_offset = r.u32();
        s.index_count = r.u32();
        s.base_color_texture = r.i32();
        s.normal_texture = r.i32();
        s.orm_texture = r.i32();
        s.palette.resize(r.count(2));
        for (uint16_t& p : s.palette) p = r.u16();
    }
}

}  // namespace

const ModelPackLod* ModelPack::lod(uint32_t max_triangles) const {
    for (const ModelPackLod& l : lods) {
        if (l.max_triangles == max_triangles) return &l;
    }
    return nullptr;
}

std::string model_pack_path(const std::string& model_path) { return model_path + ".pack"; }

gfx::TextureKind texture_kind_for(const SkinnedMeshData& mesh, const std::vector<uint8_t>& texture_srgb,
                                  size_t index) {
    for (const SkinnedSubmesh& sub : mesh.submeshes) {
        if (sub.normal_texture == int(index)) return gfx::TextureKind::NormalMap;
        if (sub.orm_texture == int(index)) return gfx::TextureKind::Orm;
    }
    const bool srgb = index < texture_srgb.size() && texture_srgb[index] != 0;
    return srgb ? gfx::TextureKind::Colour : gfx::TextureKind::Data;
}

bool build_model_pack(const char* model_path, const std::vector<uint32_t>& triangle_budgets,
                      uint32_t texture_cap, ModelPack& out, std::string* error) {
    out = ModelPack{};
    GltfLoadResult loaded = load_skinned_gltf(model_path, out.skeleton, out.mesh);
    if (!loaded.ok) {
        if (error) *error = loaded.error;
        return false;
    }
    source_fingerprint(model_path, &out.source_size, &out.source_hash);
    out.animations = std::move(loaded.animations);
    out.bounds_min = loaded.bounds_min;
    out.bounds_max = loaded.bounds_max;
    out.bind_pose_error = loaded.bind_pose_error;

    for (uint32_t budget : triangle_budgets) {
        if (budget == 0 || out.lod(budget)) continue;
        ModelPackLod lod;
        lod.max_triangles = budget;
        if (out.mesh.indices.size() / 3 > budget) {
            LodResult result = simplify_skinned(out.mesh, budget, &loaded.textures);
            lod.error = result.error;
            lod.mesh = std::move(result.mesh);
        }
        out.lods.push_back(std::move(lod));
    }

    gfx::TextureBudget budget;
    budget.max_size = texture_cap;
    budget.compress = true;
    for (size_t i = 0; i < loaded.textures.size(); ++i) {
        ModelPackTexture t;
        t.kind = texture_kind_for(out.mesh, loaded.texture_srgb, i);
        t.srgb = i < loaded.texture_srgb.size() && loaded.texture_srgb[i] != 0;
        t.prepared = gfx::prepare_texture(loaded.textures[i], t.kind, budget, true);
        // Free the decoded source as each is done: five 4096² maps at once
        // is the peak this pack exists to remove.
        loaded.textures[i] = gfx::ImageData{};
        out.textures.push_back(std::move(t));
    }
    return true;
}

bool write_model_pack(const char* path, const ModelPack& pack) {
    Writer w;
    w.raw(MAGIC, sizeof MAGIC);
    w.u32(VERSION);
    w.u64(pack.source_size);
    w.u64(pack.source_hash);

    w.u32(uint32_t(pack.skeleton.count()));
    for (const Joint& j : pack.skeleton.joints()) {
        w.str(j.name);
        w.i32(j.parent);
        w.vec3(j.local_bind.position);
        w.quat(j.local_bind.rotation);
        w.vec3(j.local_bind.scale);
        w.mat4(j.inverse_bind);
    }
    w.vec3(pack.bounds_min);
    w.vec3(pack.bounds_max);
    w.f32(pack.bind_pose_error);

    w.u32(uint32_t(pack.animations.size()));
    for (const AnimationClip& clip : pack.animations) {
        w.str(clip.name);
        w.f32(clip.duration);
        w.u32(uint32_t(clip.tracks.size()));
        for (const RotationTrack& t : clip.tracks) {
            w.i32(t.joint);
            w.u32(uint32_t(t.times.size()));
            for (float time : t.times) w.f32(time);
            for (const core::Quat& q : t.rotations) w.quat(q);
        }
    }

    write_mesh(w, pack.mesh);
    w.u32(uint32_t(pack.lods.size()));
    for (const ModelPackLod& lod : pack.lods) {
        w.u32(lod.max_triangles);
        w.f32(lod.error);
        write_mesh(w, lod.mesh);
    }

    w.u32(uint32_t(pack.textures.size()));
    for (const ModelPackTexture& t : pack.textures) {
        w.u8(uint8_t(t.kind));
        w.u8(t.srgb ? 1 : 0);
        w.u8(uint8_t(t.prepared.format));
        w.u32(t.prepared.width);
        w.u32(t.prepared.height);
        w.i32(t.prepared.source_width);
        w.i32(t.prepared.source_height);
        w.u32(uint32_t(t.prepared.levels.size()));
        for (const std::vector<uint8_t>& level : t.prepared.levels) w.bytes(level);
    }

    FILE* file = std::fopen(path, "wb");
    if (!file) {
        LOG_ERROR("model pack: cannot write %s", path);
        return false;
    }
    const bool ok = std::fwrite(w.data().data(), 1, w.data().size(), file) == w.data().size();
    std::fclose(file);
    if (!ok) LOG_ERROR("model pack: short write to %s", path);
    return ok;
}

bool read_model_pack(const char* path, ModelPack& out, std::string* error) {
    out = ModelPack{};
    FILE* file = std::fopen(path, "rb");
    if (!file) {
        if (error) *error = "no such file";
        return false;
    }
    std::vector<uint8_t> data;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size > 0) {
        data.resize(size_t(size));
        if (std::fread(data.data(), 1, data.size(), file) != data.size()) data.clear();
    }
    std::fclose(file);
    if (data.size() < sizeof MAGIC + 4) {
        if (error) *error = "truncated";
        return false;
    }
    if (std::memcmp(data.data(), MAGIC, sizeof MAGIC) != 0) {
        if (error) *error = "not a model pack";
        return false;
    }
    Reader r(std::move(data));
    for (size_t i = 0; i < sizeof MAGIC; ++i) r.u8();
    const uint32_t version = r.u32();
    if (version != VERSION) {
        if (error) *error = "version " + std::to_string(version) + ", this build reads " + std::to_string(VERSION);
        return false;
    }
    out.source_size = r.u64();
    out.source_hash = r.u64();

    const uint32_t joint_count = r.count(4 + 4 + 40 + 64);
    for (uint32_t i = 0; i < joint_count && r.ok(); ++i) {
        const std::string name = r.str();
        const int parent = r.i32();
        core::Transform local;
        local.position = r.vec3();
        local.rotation = r.quat();
        local.scale = r.vec3();
        const core::Mat4 inverse_bind = r.mat4();
        if (parent >= int(i)) {  // parents precede children, as the loader orders them
            if (error) *error = "joint order";
            return false;
        }
        const int index = out.skeleton.add_joint(name, parent, local);
        out.skeleton.set_inverse_bind(index, inverse_bind);
    }
    out.bounds_min = r.vec3();
    out.bounds_max = r.vec3();
    out.bind_pose_error = r.f32();

    out.animations.resize(r.count(4 + 4 + 4));
    for (AnimationClip& clip : out.animations) {
        clip.name = r.str();
        clip.duration = r.f32();
        clip.tracks.resize(r.count(4 + 4));
        for (RotationTrack& t : clip.tracks) {
            t.joint = r.i32();
            const uint32_t n = r.count(4 + 16);
            t.times.resize(n);
            t.rotations.resize(n);
            for (float& time : t.times) time = r.f32();
            for (core::Quat& q : t.rotations) q = r.quat();
        }
    }

    read_mesh(r, out.mesh);
    out.lods.resize(r.count(4 + 4 + 12));
    for (ModelPackLod& lod : out.lods) {
        lod.max_triangles = r.u32();
        lod.error = r.f32();
        read_mesh(r, lod.mesh);
    }

    out.textures.resize(r.count(3 + 4 + 4 + 4 + 4 + 4));
    for (ModelPackTexture& t : out.textures) {
        t.kind = gfx::TextureKind(r.u8());
        t.srgb = r.u8() != 0;
        t.prepared.format = rhi::Format(r.u8());
        t.prepared.width = r.u32();
        t.prepared.height = r.u32();
        t.prepared.source_width = r.i32();
        t.prepared.source_height = r.i32();
        t.prepared.levels.resize(r.count(4));
        for (std::vector<uint8_t>& level : t.prepared.levels) level = r.bytes();
    }

    if (!r.ok() || !r.at_end()) {
        if (error) *error = r.ok() ? "trailing bytes" : "truncated";
        return false;
    }
    return true;
}

bool source_fingerprint(const char* path, uint64_t* size, uint64_t* hash) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    if (length < 0) {
        std::fclose(file);
        return false;
    }
    constexpr long WINDOW = 256 * 1024;
    uint64_t h = 14695981039346656037ull;
    const auto mix = [&](long from, long count) {
        std::vector<uint8_t> buf(static_cast<size_t>(count), 0);
        std::fseek(file, from, SEEK_SET);
        const size_t got = std::fread(buf.data(), 1, buf.size(), file);
        for (size_t i = 0; i < got; ++i) h = (h ^ buf[i]) * 1099511628211ull;
    };
    mix(0, length < WINDOW ? length : WINDOW);
    if (length > WINDOW) mix(length - WINDOW, WINDOW);
    std::fclose(file);
    *size = uint64_t(length);
    *hash = h;
    return true;
}

bool model_pack_fresh(const ModelPack& pack, const char* source_path) {
    uint64_t size = 0, hash = 0;
    if (!source_fingerprint(source_path, &size, &hash)) return true;
    return size == pack.source_size && hash == pack.source_hash;
}

}  // namespace anim

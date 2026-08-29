#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace audio {

// Every sound in the game, synthesized at init -- no audio assets, in the same
// spirit as the procedural terrain. Two kinds of sound: continuous streams
// (wind, flame) whose levels the game sets every frame, and one-shot clips
// (explosion, shot, hit, wingbeat) fired at events.
enum class Clip : int {
    Explosion = 0,
    Shot,
    Hit,
    Flap,
    Count,
};

class Audio {
public:
    // Returns false if no output device exists; the game runs silent, not dead.
    bool init();
    void shutdown();
    bool ready() const { return ready_; }

    // 0..1, smoothed inside the mixer so per-frame calls cannot zipper.
    void set_wind(float level) { wind_target_.store(level, std::memory_order_relaxed); }
    void set_flame(float level) { flame_target_.store(level, std::memory_order_relaxed); }
    void set_master(float volume) { master_.store(volume, std::memory_order_relaxed); }
    float master() const { return master_.load(std::memory_order_relaxed); }

    void play(Clip clip, float gain);

    // The mixer callback. Public only because the C callback trampoline needs
    // it; nothing else should call it.
    void mix(float* out, uint32_t frames);

private:
    void synthesize_clips();

    struct Voice {
        std::atomic<bool> active{false};
        const std::vector<float>* samples = nullptr;
        uint32_t cursor = 0;
        float gain = 1.0f;
    };

    static constexpr int MAX_VOICES = 12;
    Voice voices_[MAX_VOICES];
    std::vector<float> clips_[int(Clip::Count)];

    std::atomic<float> wind_target_{0.0f};
    std::atomic<float> flame_target_{0.0f};
    std::atomic<float> master_{0.6f};

    // Mixer-thread state (only touched inside mix()).
    float wind_level_ = 0.0f;
    float flame_level_ = 0.0f;
    float wind_lp_ = 0.0f;
    float flame_lp_ = 0.0f;
    float flame_phase_ = 0.0f;
    uint32_t noise_state_ = 0x1234567u;

    void* device_ = nullptr;  // ma_device*, kept opaque to spare every includer
    bool ready_ = false;
};

}  // namespace audio

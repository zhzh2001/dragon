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
    Screech,   // taking a hit: a wounded-animal cry, not a UI ping
    KnockOut,  // going down: a long dying cry over a heavy thump
    Boost,     // a rising rush of air
    Bite,      // the swing: a whoosh rising into the snap of jaws
    BiteHit,   // the swing connecting: a crunch and a heavy thud
    Flap,
    // The elements, one each for a status landing hard: a flare catching,
    // ice shattering, acid hissing, a lightning crack, a splash, rock
    // breaking. Synthesized like everything else; a hit you can hear the
    // element of is a hit you do not have to look at.
    Ignite,
    Shatter,
    Hiss,
    Zap,
    Splash,
    Crack,
    // The fury: a deep boom with a rising roar behind it.
    Fury,
    // The PLAYER's own voice, told apart from the bots' screech: a short
    // chesty growl-roar when hit, and a long falling roar when downed. The
    // playtest could not tell its own cry from a rival's.
    Roar,
    RoarDown,
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
    // What the held breath sounds like: 0 fire (a roar with crackle), 1
    // frost (a bright hiss with glassy sparkle), 2 blight (a bubbling
    // gurgle), 3 storm (an electric buzz), 4 tide (rushing, sloshing water),
    // 5 stone (gritty rumble). The element enum's order.
    void set_flame_style(int style) { flame_style_.store(style, std::memory_order_relaxed); }
    void set_master(float volume) { master_.store(volume, std::memory_order_relaxed); }
    float master() const { return master_.load(std::memory_order_relaxed); }

    // `rate` is playback speed: 1 is as synthesized, lower is deeper and
    // slower. Cheap per-voice pitch, which is what makes one screech clip serve
    // every dragon in the sky without them chorusing.
    void play(Clip clip, float gain, float rate = 1.0f);

    // The mixer callback. Public only because the C callback trampoline needs
    // it; nothing else should call it.
    void mix(float* out, uint32_t frames);

private:
    void synthesize_clips();

    struct Voice {
        std::atomic<bool> active{false};
        const std::vector<float>* samples = nullptr;
        float cursor = 0.0f;
        float rate = 1.0f;
        float gain = 1.0f;
    };

    static constexpr int MAX_VOICES = 12;
    Voice voices_[MAX_VOICES];
    std::vector<float> clips_[int(Clip::Count)];

    std::atomic<float> wind_target_{0.0f};
    std::atomic<float> flame_target_{0.0f};
    std::atomic<int> flame_style_{0};
    float buzz_phase_ = 0.0f;
    float slosh_phase_ = 0.0f;
    float flame_hp_[2] = {0.0f, 0.0f};
    std::atomic<float> master_{0.6f};

    // Mixer-thread state (only touched inside mix()). Wind and flame carry a
    // filter per channel with independent noise, which is what makes the sound
    // wide: identical channels collapse to mono in the head.
    float wind_level_ = 0.0f;
    float flame_level_ = 0.0f;
    float wind_lp_[2] = {0.0f, 0.0f};
    float wind_rumble_[2] = {0.0f, 0.0f};
    float flame_lp_[2] = {0.0f, 0.0f};
    float gust_phase_ = 0.0f;
    float flame_phase_ = 0.0f;
    float crackle_hold_ = 0.0f;
    uint32_t noise_state_[2] = {0x1234567u, 0x89abcdefu};

    void* device_ = nullptr;  // ma_device*, kept opaque to spare every includer
    bool ready_ = false;
};

}  // namespace audio

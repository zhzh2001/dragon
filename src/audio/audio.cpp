#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include <miniaudio.h>

#include "audio/audio.h"

#include <cmath>

#include "core/log.h"
#include "core/math.h"

namespace audio {

namespace {

constexpr uint32_t SAMPLE_RATE = 48000;
constexpr float TWO_PI = 6.28318530718f;

// One shared xorshift for synthesis (init-time only; the mixer has its own).
uint32_t g_synth_rng = 0x9e3779b9u;
float synth_noise() {
    g_synth_rng ^= g_synth_rng << 13;
    g_synth_rng ^= g_synth_rng >> 17;
    g_synth_rng ^= g_synth_rng << 5;
    return float(g_synth_rng & 0xffffffu) / float(0x7fffff) - 1.0f;
}

void data_callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    static_cast<Audio*>(device->pUserData)->mix(static_cast<float*>(output), frames);
}

}  // namespace

void Audio::synthesize_clips() {
    const auto seconds = [](float s) { return uint32_t(s * SAMPLE_RATE); };

    // Explosion: a sub-bass sine thump under a brown-noise burst, both dying
    // exponentially. Brown (integrated) noise because white noise reads as
    // static, not as a blast wave.
    {
        std::vector<float>& clip = clips_[int(Clip::Explosion)];
        clip.resize(seconds(0.9f));
        float brown = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            brown += synth_noise() * 0.15f;
            brown *= 0.995f;
            const float body = brown * std::exp(-4.5f * t);
            const float thump = 0.8f * std::sin(TWO_PI * 48.0f * t * (1.0f - 0.4f * t)) *
                                std::exp(-6.0f * t);
            clip[i] = 0.9f * (body + thump);
        }
    }

    // Shot: a noise whoosh sweeping down through a resonant filter -- the sound
    // of something hot leaving fast.
    {
        std::vector<float>& clip = clips_[int(Clip::Shot)];
        clip.resize(seconds(0.35f));
        float lp = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            const float u = t / 0.35f;
            const float cutoff = 0.35f - 0.28f * u;  // one-pole coefficient sweep
            lp += (synth_noise() - lp) * cutoff;
            const float envelope = std::sin(3.14159f * core::minf(u * 1.15f, 1.0f));
            clip[i] = 0.7f * lp * envelope;
        }
    }

    // Hit: a short low ping with a click of noise on the front. Felt more than
    // heard, which is what taking damage should be.
    {
        std::vector<float>& clip = clips_[int(Clip::Hit)];
        clip.resize(seconds(0.25f));
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            const float ping = std::sin(TWO_PI * 95.0f * t) * std::exp(-14.0f * t);
            const float click = synth_noise() * std::exp(-90.0f * t) * 0.5f;
            clip[i] = 0.8f * (ping + click);
        }
    }

    // Wingbeat: a broad low whoosh, swelling then gone -- air moved, not tone.
    {
        std::vector<float>& clip = clips_[int(Clip::Flap)];
        clip.resize(seconds(0.45f));
        float lp = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            const float u = t / 0.45f;
            lp += (synth_noise() - lp) * 0.06f;
            const float envelope = std::sin(3.14159f * u);
            clip[i] = 1.4f * lp * envelope * envelope;
        }
    }
}

bool Audio::init() {
    synthesize_clips();

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = data_callback;
    config.pUserData = this;

    ma_device* device = new ma_device;
    if (ma_device_init(nullptr, &config, device) != MA_SUCCESS) {
        LOG_WARN("audio device unavailable; running silent");
        delete device;
        return false;
    }
    if (ma_device_start(device) != MA_SUCCESS) {
        LOG_WARN("audio device failed to start; running silent");
        ma_device_uninit(device);
        delete device;
        return false;
    }
    device_ = device;
    ready_ = true;
    LOG_INFO("audio: %u Hz, all sounds synthesized (no assets)", SAMPLE_RATE);
    return true;
}

void Audio::shutdown() {
    if (!device_) return;
    ma_device* device = static_cast<ma_device*>(device_);
    ma_device_uninit(device);
    delete device;
    device_ = nullptr;
    ready_ = false;
}

void Audio::play(Clip clip, float gain) {
    if (!ready_ || gain < 0.01f) return;
    const std::vector<float>& samples = clips_[int(clip)];
    if (samples.empty()) return;
    // First inactive voice wins; a fight loud enough to exhaust twelve voices
    // will not miss one more explosion.
    for (Voice& voice : voices_) {
        if (voice.active.load(std::memory_order_acquire)) continue;
        voice.samples = &samples;
        voice.cursor = 0;
        voice.gain = core::minf(gain, 1.5f);
        voice.active.store(true, std::memory_order_release);
        return;
    }
}

void Audio::mix(float* out, uint32_t frames) {
    const float master = master_.load(std::memory_order_relaxed);
    const float wind_target = wind_target_.load(std::memory_order_relaxed);
    const float flame_target = flame_target_.load(std::memory_order_relaxed);

    for (uint32_t i = 0; i < frames; ++i) {
        // Smooth the control levels at audio rate: per-frame game updates would
        // otherwise zipper.
        wind_level_ += (wind_target - wind_level_) * 0.0004f;
        flame_level_ += (flame_target - flame_level_) * 0.002f;

        noise_state_ ^= noise_state_ << 13;
        noise_state_ ^= noise_state_ >> 17;
        noise_state_ ^= noise_state_ << 5;
        const float noise = float(noise_state_ & 0xffffffu) / float(0x7fffff) - 1.0f;

        // Wind: noise through a one-pole lowpass whose cutoff opens with speed.
        // Faster flight is not just louder, it is brighter -- that is what makes
        // a dive audibly build.
        const float wind_cutoff = 0.02f + 0.12f * wind_level_;
        wind_lp_ += (noise - wind_lp_) * wind_cutoff;
        float sample = wind_lp_ * wind_level_ * wind_level_ * 1.6f;

        // Flame: darker noise with a slow crackle wobble under it.
        flame_phase_ += TWO_PI * 31.0f / SAMPLE_RATE;
        flame_lp_ += (noise - flame_lp_) * 0.05f;
        sample += flame_lp_ * flame_level_ *
                  (0.7f + 0.3f * std::sin(flame_phase_)) * 0.9f;

        // One-shots.
        for (Voice& voice : voices_) {
            if (!voice.active.load(std::memory_order_acquire)) continue;
            sample += (*voice.samples)[voice.cursor] * voice.gain;
            if (++voice.cursor >= voice.samples->size()) {
                voice.active.store(false, std::memory_order_release);
            }
        }

        // Soft clip: a saturating tanh keeps a busy fight loud but never harsh.
        out[i] = std::tanh(sample * master);
    }
}

}  // namespace audio

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

    // Screech: a wounded-animal cry -- a pitch falling through harmonics with
    // vibrato and breath noise. FM-ish synthesis, because a pure sine reads as
    // a UI beep and taking a hit should sound like it happened to a creature.
    auto screech = [&](std::vector<float>& clip, float duration, float f_start, float f_end,
                       float loudness) {
        clip.resize(seconds(duration));
        float phase = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            const float u = t / duration;
            const float vibrato = 1.0f + 0.045f * std::sin(TWO_PI * 26.0f * t);
            const float f = (f_start + (f_end - f_start) * u * u) * vibrato;
            phase += TWO_PI * f / SAMPLE_RATE;
            // A reedy waveform: fundamental plus strong odd harmonics.
            float tone = std::sin(phase) + 0.55f * std::sin(2.0f * phase + 0.7f) +
                         0.3f * std::sin(3.0f * phase);
            // Breath: noise amplitude-modulated by the tone, which fuses them
            // into one voice instead of a whistle plus static.
            tone += synth_noise() * 0.25f * (0.5f + 0.5f * std::fabs(tone));
            const float envelope = std::sin(3.14159f * core::minf(u * 1.2f, 1.0f));
            clip[i] = loudness * 0.30f * tone * envelope;
        }
    };
    screech(clips_[int(Clip::Screech)], 0.38f, 1350.0f, 750.0f, 1.0f);

    // Knock-out: a long, falling dying cry over a heavy body thump, with room
    // to trail off -- a death should take a moment.
    {
        std::vector<float>& clip = clips_[int(Clip::KnockOut)];
        screech(clip, 1.8f, 1050.0f, 190.0f, 1.1f);
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            clip[i] += 0.7f * std::sin(TWO_PI * 42.0f * t) * std::exp(-3.0f * t);
        }
    }

    // Boost: a rush of air sweeping upward -- the filter opens instead of
    // closing, which is what makes it read as acceleration.
    {
        std::vector<float>& clip = clips_[int(Clip::Boost)];
        clip.resize(seconds(0.7f));
        float lp = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            const float u = t / 0.7f;
            const float cutoff = 0.04f + 0.30f * u;
            lp += (synth_noise() - lp) * cutoff;
            const float envelope = std::sin(3.14159f * core::minf(u * 1.1f, 1.0f));
            clip[i] = 0.85f * lp * envelope * (1.0f + 0.5f * u);
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
    config.playback.channels = 2;
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

void Audio::play(Clip clip, float gain, float rate) {
    if (!ready_ || gain < 0.01f) return;
    const std::vector<float>& samples = clips_[int(clip)];
    if (samples.empty()) return;
    // First inactive voice wins; a fight loud enough to exhaust twelve voices
    // will not miss one more explosion.
    for (Voice& voice : voices_) {
        if (voice.active.load(std::memory_order_acquire)) continue;
        voice.samples = &samples;
        voice.cursor = 0.0f;
        voice.rate = core::clampf(rate, 0.5f, 2.0f);
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
        wind_level_ += (wind_target - wind_level_) * 0.0004f;
        flame_level_ += (flame_target - flame_level_) * 0.002f;

        // A slow gust LFO on top of the commanded level: steady wind sounds
        // like a fan, gusting wind sounds like weather.
        gust_phase_ += TWO_PI * 0.23f / SAMPLE_RATE;
        const float gust = 1.0f + 0.22f * std::sin(gust_phase_) *
                                      std::sin(gust_phase_ * 2.7f + 1.3f);
        flame_phase_ += TWO_PI * 31.0f / SAMPLE_RATE;

        // Flame crackle: sparse random pops held for a few samples, gated by
        // the flame level. This is what separates fire from filtered static.
        if (crackle_hold_ > 0.0f) {
            crackle_hold_ -= 1.0f;
        }

        // One-shots are mono, mixed to both ears.
        float voices = 0.0f;
        for (Voice& voice : voices_) {
            if (!voice.active.load(std::memory_order_acquire)) continue;
            voices += (*voice.samples)[uint32_t(voice.cursor)] * voice.gain;
            voice.cursor += voice.rate;
            if (uint32_t(voice.cursor) >= voice.samples->size()) {
                voice.active.store(false, std::memory_order_release);
            }
        }

        for (int channel = 0; channel < 2; ++channel) {
            uint32_t& rng = noise_state_[channel];
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            const float noise = float(rng & 0xffffffu) / float(0x7fffff) - 1.0f;

            // Wind, two layers per ear: a bright hiss whose cutoff opens with
            // speed (a dive gets brighter, not merely louder) over a low
            // buffet rumble. Independent noise per channel is what makes it
            // wide; identical channels collapse to mono in the head.
            const float wind = wind_level_ * gust;
            const float hiss_cutoff = 0.02f + 0.13f * wind;
            wind_lp_[channel] += (noise - wind_lp_[channel]) * hiss_cutoff;
            wind_rumble_[channel] += (noise - wind_rumble_[channel]) * 0.006f;
            float sample = wind_lp_[channel] * wind * wind * 1.5f +
                           wind_rumble_[channel] * wind * 2.2f;

            // Flame: dark roaring noise with a slow wobble and crackle pops.
            flame_lp_[channel] += (noise - flame_lp_[channel]) * 0.05f;
            float flame = flame_lp_[channel] * (0.7f + 0.3f * std::sin(flame_phase_));
            if (channel == 0 && flame_level_ > 0.05f && (rng & 0x3ffu) == 0u) {
                crackle_hold_ = 90.0f;  // ~2 ms pop
            }
            if (crackle_hold_ > 0.0f) flame += noise * 0.6f;
            sample += flame * flame_level_ * 0.9f;

            out[i * 2 + channel] = std::tanh((sample + voices) * master);
        }
    }
}

}  // namespace audio

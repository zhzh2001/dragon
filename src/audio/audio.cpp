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

    // Bite, the swing: a whoosh rising for a fifth of a second (the neck
    // coming through the air) that ends in the snap -- a bright click over a
    // short low thud. The first cut was the snap alone, 0.2 s, and in play it
    // vanished under the flap; the whoosh is what makes it a gesture you can
    // hear starting.
    {
        std::vector<float>& clip = clips_[int(Clip::Bite)];
        clip.resize(seconds(0.42f));
        float lp = 0.0f;
        const float snap_at = 0.2f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            // Whoosh: filtered noise whose cutoff rises toward the snap.
            const float u = core::saturate(t / snap_at);
            const float cutoff = 0.05f + 0.35f * u * u;
            lp += (synth_noise() - lp) * cutoff;
            const float whoosh = lp * std::sin(3.14159f * core::minf(u, 1.0f)) * 0.9f;
            float snap = 0.0f;
            if (t >= snap_at) {
                const float s = t - snap_at;
                snap = synth_noise() * std::exp(-260.0f * s) * 1.4f +
                       0.9f * std::sin(TWO_PI * 85.0f * s) * std::exp(-16.0f * s);
            }
            clip[i] = 0.9f * (whoosh + snap);
        }
    }

    // Bite landing: a crunch -- a burst of dense noise chewed by a fast
    // tremolo -- over a heavy thud, longer and lower than the swing's snap so
    // a hit and a miss are told apart with the eyes shut.
    {
        std::vector<float>& clip = clips_[int(Clip::BiteHit)];
        clip.resize(seconds(0.38f));
        float brown = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            brown += synth_noise() * 0.35f;
            brown *= 0.97f;
            const float tremolo = 0.6f + 0.4f * std::sin(TWO_PI * 38.0f * t);
            const float crunch = brown * tremolo * std::exp(-9.0f * t);
            const float thud = 1.0f * std::sin(TWO_PI * 60.0f * t * (1.0f - 0.3f * t)) *
                               std::exp(-7.0f * t);
            clip[i] = 0.95f * (crunch + thud);
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

    // ---- the elements ----
    // Ignite: a quick rising whoosh into a soft roar -- something catching.
    {
        std::vector<float>& clip = clips_[int(Clip::Ignite)];
        clip.resize(seconds(0.5f));
        float lp = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            const float u = t / 0.5f;
            lp += (synth_noise() - lp) * (0.04f + 0.2f * u);
            const float env = core::minf(t * 20.0f, 1.0f) * std::exp(-3.5f * t);
            clip[i] = 1.1f * lp * env;
        }
    }
    // Shatter: glassy -- a cluster of high, inharmonic partials struck and
    // ringing out, over a crunch of white noise.
    {
        std::vector<float>& clip = clips_[int(Clip::Shatter)];
        clip.resize(seconds(0.7f));
        const float partials[6] = {2350.0f, 3120.0f, 3890.0f, 4610.0f, 5570.0f, 6930.0f};
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            float ring = 0.0f;
            for (int k = 0; k < 6; ++k) {
                ring += std::sin(TWO_PI * partials[k] * t + float(k)) * std::exp(-(5.0f + 2.0f * float(k)) * t);
            }
            const float crunch = synth_noise() * std::exp(-30.0f * t);
            clip[i] = 0.16f * ring + 0.6f * crunch;
        }
    }
    // Hiss: acid on hide -- a long filtered hiss with bubbling pops.
    {
        std::vector<float>& clip = clips_[int(Clip::Hiss)];
        clip.resize(seconds(0.8f));
        float hp = 0.0f, prev = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            const float n = synth_noise();
            hp = 0.92f * (hp + n - prev);  // a high-pass: the hiss
            prev = n;
            const float bubble = std::sin(TWO_PI * (180.0f + 90.0f * std::sin(TWO_PI * 11.0f * t)) * t) *
                                 (0.5f + 0.5f * std::sin(TWO_PI * 13.0f * t));
            const float env = core::minf(t * 12.0f, 1.0f) * std::exp(-3.0f * t);
            clip[i] = env * (0.35f * hp + 0.3f * bubble);
        }
    }
    // Zap: a lightning crack -- a hard noise transient, then a buzzing
    // sawtooth dropping in pitch.
    {
        std::vector<float>& clip = clips_[int(Clip::Zap)];
        clip.resize(seconds(0.45f));
        float phase = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            const float f = 900.0f * std::exp(-5.0f * t) + 110.0f;
            phase += f / SAMPLE_RATE;
            const float saw = 2.0f * (phase - std::floor(phase)) - 1.0f;
            const float crack = synth_noise() * std::exp(-60.0f * t);
            clip[i] = 0.9f * crack + 0.35f * saw * std::exp(-6.0f * t);
        }
    }
    // Splash: a low slap and a spray of mid noise that falls away.
    {
        std::vector<float>& clip = clips_[int(Clip::Splash)];
        clip.resize(seconds(0.6f));
        float lp = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            lp += (synth_noise() - lp) * (0.25f - 0.2f * core::saturate(t / 0.6f));
            const float slap = std::sin(TWO_PI * 140.0f * t) * std::exp(-25.0f * t);
            clip[i] = 0.8f * slap + 0.9f * lp * std::exp(-5.0f * t) * core::minf(t * 60.0f, 1.0f);
        }
    }
    // Crack: rock breaking -- a sharp split over a deep, gritty thud.
    {
        std::vector<float>& clip = clips_[int(Clip::Crack)];
        clip.resize(seconds(0.55f));
        float brown = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            brown += synth_noise() * 0.3f;
            brown *= 0.985f;
            const float split = synth_noise() * std::exp(-80.0f * t) * 1.2f;
            const float thud = std::sin(TWO_PI * 55.0f * t * (1.0f - 0.4f * t)) * std::exp(-6.0f * t);
            const float grit = brown * std::exp(-8.0f * t) * (0.6f + 0.4f * (synth_noise() > 0.6f));
            clip[i] = 0.9f * (split + thud + grit);
        }
    }
    // Fury: a sub boom, then a roar rising as the nova spreads.
    {
        std::vector<float>& clip = clips_[int(Clip::Fury)];
        clip.resize(seconds(1.6f));
        float brown = 0.0f, lp = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            brown += synth_noise() * 0.2f;
            brown *= 0.996f;
            lp += (synth_noise() - lp) * (0.03f + 0.15f * core::saturate(t / 1.2f));
            const float boom = std::sin(TWO_PI * 38.0f * t * (1.0f - 0.3f * t)) * std::exp(-2.5f * t);
            const float roar = lp * std::sin(3.14159f * core::saturate(t / 1.6f)) * 1.3f;
            clip[i] = 0.9f * (boom + 0.6f * brown * std::exp(-2.0f * t) + roar);
        }
    }

    // The player's roar: a low sawtooth growl (a big chest, not a throat)
    // with a rough amplitude flutter and breath, through a gentle low-pass --
    // everything the bots' reedy, falling screech is not.
    auto roar = [&](std::vector<float>& clip, float duration, float f_start, float f_end, float loudness) {
        clip.resize(seconds(duration));
        float phase = 0.0f, lp = 0.0f;
        for (uint32_t i = 0; i < clip.size(); ++i) {
            const float t = float(i) / SAMPLE_RATE;
            const float u = t / duration;
            const float f = f_start + (f_end - f_start) * u;
            phase += f / SAMPLE_RATE;
            const float saw = 2.0f * (phase - std::floor(phase)) - 1.0f;
            const float flutter = 0.65f + 0.35f * std::sin(TWO_PI * 31.0f * t) * std::sin(TWO_PI * 7.0f * t + 1.1f);
            const float raw = saw * flutter + synth_noise() * 0.35f;
            lp += (raw - lp) * 0.12f;
            const float envelope = core::minf(t * 25.0f, 1.0f) * std::pow(1.0f - u, 1.5f);
            clip[i] = loudness * 0.75f * lp * envelope;
        }
    };
    roar(clips_[int(Clip::Roar)], 0.34f, 150.0f, 105.0f, 1.0f);
    roar(clips_[int(Clip::RoarDown)], 1.6f, 140.0f, 60.0f, 1.2f);

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

            // The held breath, voiced by its element. Fire: dark roaring
            // noise with a slow wobble and crackle pops.
            const int style = flame_style_.load(std::memory_order_relaxed);
            float flame = 0.0f;
            switch (style) {
                case 1: {  // frost: a bright hiss with glassy glints
                    flame_hp_[channel] = 0.9f * (flame_hp_[channel] + noise - flame_lp_[channel]);
                    flame_lp_[channel] = noise;
                    flame = flame_hp_[channel] * 0.45f;
                    if ((rng & 0x7ffu) == 0u) crackle_hold_ = 30.0f;
                    if (crackle_hold_ > 0.0f) flame += std::sin(TWO_PI * 5200.0f * float(i) / SAMPLE_RATE) * 0.4f;
                    break;
                }
                case 2: {  // blight: a bubbling gurgle
                    flame_lp_[channel] += (noise - flame_lp_[channel]) * 0.03f;
                    slosh_phase_ += TWO_PI * 7.0f / SAMPLE_RATE / 2.0f;
                    flame = flame_lp_[channel] * 1.3f * (0.5f + 0.5f * std::sin(slosh_phase_)) +
                            0.15f * std::sin(TWO_PI * (160.0f + 60.0f * std::sin(slosh_phase_ * 1.7f)) *
                                             float(i) / SAMPLE_RATE);
                    break;
                }
                case 3: {  // storm: an electric buzz with crackle
                    buzz_phase_ += 118.0f / SAMPLE_RATE / 2.0f;
                    const float saw = 2.0f * (buzz_phase_ - std::floor(buzz_phase_)) - 1.0f;
                    flame = 0.35f * saw * (0.7f + 0.3f * noise);
                    if (channel == 0 && (rng & 0xffu) == 0u) crackle_hold_ = 60.0f;
                    if (crackle_hold_ > 0.0f) flame += noise * 0.7f;
                    break;
                }
                case 4: {  // tide: rushing water that sloshes
                    flame_lp_[channel] += (noise - flame_lp_[channel]) * 0.12f;
                    slosh_phase_ += TWO_PI * 2.6f / SAMPLE_RATE / 2.0f;
                    flame = flame_lp_[channel] * (0.55f + 0.45f * std::sin(slosh_phase_));
                    break;
                }
                case 5: {  // stone: a gritty low rumble, thick with grit
                    flame_lp_[channel] += (noise - flame_lp_[channel]) * 0.015f;
                    flame = flame_lp_[channel] * 2.0f;
                    if ((rng & 0x7fu) == 0u) crackle_hold_ = 25.0f;
                    if (crackle_hold_ > 0.0f) flame += noise * 0.35f;
                    break;
                }
                default: {
                    flame_lp_[channel] += (noise - flame_lp_[channel]) * 0.05f;
                    flame = flame_lp_[channel] * (0.7f + 0.3f * std::sin(flame_phase_));
                    if (channel == 0 && flame_level_ > 0.05f && (rng & 0x3ffu) == 0u) {
                        crackle_hold_ = 90.0f;  // ~2 ms pop
                    }
                    if (crackle_hold_ > 0.0f) flame += noise * 0.6f;
                    break;
                }
            }
            sample += flame * flame_level_ * 0.9f;

            out[i * 2 + channel] = std::tanh((sample + voices) * master);
        }
    }
}

}  // namespace audio

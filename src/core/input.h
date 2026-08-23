#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>

#include "core/math.h"

namespace core {

// Per-frame input snapshot.
//
// Edge state (pressed/released) is derived here rather than read from events, so
// gameplay code can ask "was this key pressed this frame" at any point in the
// update without caring where in the frame it runs.
class Input {
public:
    // Call once per frame, before polling events.
    void begin_frame();

    // Feed every SDL event. `consumed` suppresses the event, which is how UI
    // interaction stops the camera from also reacting to the same drag.
    void handle_event(const SDL_Event& event, bool consumed);

    bool down(SDL_Scancode key) const { return current_[key]; }
    bool pressed(SDL_Scancode key) const { return current_[key] && !previous_[key]; }
    bool released(SDL_Scancode key) const { return !current_[key] && previous_[key]; }

    // Returns 1 if `positive` is held, -1 if `negative` is, 0 if neither or both.
    float axis(SDL_Scancode negative, SDL_Scancode positive) const {
        return (down(positive) ? 1.0f : 0.0f) - (down(negative) ? 1.0f : 0.0f);
    }

    // ---- gamepad ----
    // Polled rather than event-driven: for continuous axes the current value is
    // all anyone wants, and polling avoids tracking per-axis state.
    bool has_gamepad() const { return gamepad_ != nullptr; }
    const char* gamepad_name() const;

    // Axis in [-1, 1] with the deadzone removed and rescaled, so the usable
    // range starts at 0 rather than jumping at the deadzone edge.
    float gamepad_axis(SDL_GamepadAxis axis, float deadzone = 0.12f) const;
    // Triggers report [0, 1].
    float gamepad_trigger(SDL_GamepadAxis axis, float deadzone = 0.06f) const;
    bool gamepad_button(SDL_GamepadButton button) const;

    bool mouse_down(int button) const { return mouse_current_ & SDL_BUTTON_MASK(button); }
    bool mouse_pressed(int button) const {
        return (mouse_current_ & SDL_BUTTON_MASK(button)) &&
               !(mouse_previous_ & SDL_BUTTON_MASK(button));
    }

    // Mouse movement this frame, in pixels.
    Vec2 mouse_delta() const { return mouse_delta_; }
    float wheel() const { return wheel_; }

private:
    bool current_[SDL_SCANCODE_COUNT] = {};
    bool previous_[SDL_SCANCODE_COUNT] = {};
    uint32_t mouse_current_ = 0;
    uint32_t mouse_previous_ = 0;
    Vec2 mouse_delta_;
    float wheel_ = 0.0f;

    // First connected pad wins. Multiple pads would only matter for local
    // multiplayer, which this game will never have.
    SDL_Gamepad* gamepad_ = nullptr;
    SDL_JoystickID gamepad_id_ = 0;
};

}  // namespace core

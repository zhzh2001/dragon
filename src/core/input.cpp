#include "core/input.h"

#include <cstring>

#include "core/log.h"

namespace core {

const char* Input::gamepad_name() const {
    if (!gamepad_) return "none";
    const char* name = SDL_GetGamepadName(gamepad_);
    return name ? name : "gamepad";
}

float Input::gamepad_axis(SDL_GamepadAxis axis, float deadzone) const {
    if (!gamepad_) return 0.0f;
    const float raw = float(SDL_GetGamepadAxis(gamepad_, axis)) / 32767.0f;
    const float magnitude = std::fabs(raw);
    if (magnitude <= deadzone) return 0.0f;
    // Rescale so the value ramps from 0 at the deadzone edge instead of
    // snapping to the deadzone value.
    const float scaled = (magnitude - deadzone) / (1.0f - deadzone);
    return signf(raw) * clampf(scaled, 0.0f, 1.0f);
}

float Input::gamepad_trigger(SDL_GamepadAxis axis, float deadzone) const {
    if (!gamepad_) return 0.0f;
    const float raw = float(SDL_GetGamepadAxis(gamepad_, axis)) / 32767.0f;
    if (raw <= deadzone) return 0.0f;
    return clampf((raw - deadzone) / (1.0f - deadzone), 0.0f, 1.0f);
}

bool Input::gamepad_button(SDL_GamepadButton button) const {
    return gamepad_ && SDL_GetGamepadButton(gamepad_, button);
}

void Input::begin_frame() {
    std::memcpy(previous_, current_, sizeof(current_));
    mouse_previous_ = mouse_current_;
    mouse_delta_ = Vec2{0, 0};
    wheel_ = 0.0f;
}

void Input::handle_event(const SDL_Event& event, bool consumed) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            // Key-up is always recorded even when consumed: dropping it would
            // leave the key stuck down after the UI takes focus mid-press.
            if (event.type == SDL_EVENT_KEY_UP) {
                current_[event.key.scancode] = false;
            } else if (!consumed && !event.key.repeat) {
                current_[event.key.scancode] = true;
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (!consumed) mouse_current_ |= SDL_BUTTON_MASK(event.button.button);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            mouse_current_ &= ~SDL_BUTTON_MASK(event.button.button);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (!consumed) {
                mouse_delta_.x += event.motion.xrel;
                mouse_delta_.y += event.motion.yrel;
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            if (!consumed) wheel_ += event.wheel.y;
            break;

        // Focus loss would otherwise leave keys latched down forever.
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            std::memset(current_, 0, sizeof(current_));
            mouse_current_ = 0;
            break;

        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad_) {
                gamepad_ = SDL_OpenGamepad(event.gdevice.which);
                if (gamepad_) {
                    gamepad_id_ = event.gdevice.which;
                    LOG_INFO("gamepad connected: %s", gamepad_name());
                }
            }
            break;

        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad_ && event.gdevice.which == gamepad_id_) {
                SDL_CloseGamepad(gamepad_);
                gamepad_ = nullptr;
                gamepad_id_ = 0;
                LOG_INFO("gamepad disconnected");
            }
            break;

        default:
            break;
    }
}

}  // namespace core

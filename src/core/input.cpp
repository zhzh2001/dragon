#include "core/input.h"

#include <cstring>

namespace core {

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

        default:
            break;
    }
}

}  // namespace core

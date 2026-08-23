// Camera orientation round-trips. This file exists because a yaw sign error
// produces a camera that looks plausibly at *something*, just never at what you
// asked for -- exactly the kind of bug that survives a glance at a screenshot.
#include <cmath>
#include <cstdio>

#include "game/debug_camera.h"

using namespace core;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line) {
    ++g_checks;
    if (!condition) {
        std::printf("  FAIL (line %d): %s\n", line, what);
        ++g_failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near(Vec3 a, Vec3 b, float eps = 1e-4f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// After set_position, the camera must face the target it was given.
void check_looks_at(Vec3 eye, Vec3 target, const char* label) {
    game::DebugCamera camera;
    camera.set_position(eye, target);
    Vec3 expected = normalize(target - eye);
    Vec3 actual = camera.camera().forward();
    std::printf("  %-22s want (%.3f %.3f %.3f) got (%.3f %.3f %.3f)\n", label, expected.x,
                expected.y, expected.z, actual.x, actual.y, actual.z);
    CHECK(near(actual, expected, 1e-3f));
    // Free-fly camera must never roll: its right vector stays horizontal.
    CHECK(near(camera.camera().right().y, 0.0f, 1e-3f));
}

void test_set_position() {
    std::printf("set_position faces the target\n");
    check_looks_at(Vec3{0, 0, 10}, Vec3::zero(), "down -Z");
    check_looks_at(Vec3{0, 0, -10}, Vec3::zero(), "down +Z");
    check_looks_at(Vec3{10, 0, 0}, Vec3::zero(), "down -X");
    check_looks_at(Vec3{-10, 0, 0}, Vec3::zero(), "down +X");
    // The exact view the app opens with -- the case that was wrong.
    check_looks_at(Vec3{18, 12, 26}, Vec3{0, 2, 0}, "app default view");
    check_looks_at(Vec3{-7, 30, 3}, Vec3{4, -2, -9}, "steep descent");
    check_looks_at(Vec3{5, -8, -2}, Vec3{-3, 6, 11}, "steep climb");
}

void test_mouse_look() {
    std::printf("mouse look directions\n");
    // Looking down -Z, dragging the mouse right must turn the view right, which
    // in a right-handed Y-up world means toward +X.
    game::DebugCamera camera;
    camera.set_position(Vec3::zero(), Vec3::forward());

    Input input;  // no events fed: all keys up, deltas zero
    camera.update(input, 1.0f / 60.0f, false);
    CHECK(near(camera.camera().forward(), Vec3::forward(), 1e-3f));

    // Feeding a synthetic motion event is the only way to exercise the look
    // path, since Input derives its deltas from events.
    SDL_Event event = {};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel = 100.0f;
    event.motion.yrel = 0.0f;
    input.begin_frame();
    input.handle_event(event, false);
    camera.update(input, 1.0f / 60.0f, true);
    Vec3 after_right_drag = camera.camera().forward();
    std::printf("  drag right -> fwd (%.3f %.3f %.3f)\n", after_right_drag.x, after_right_drag.y,
                after_right_drag.z);
    CHECK(after_right_drag.x > 0.0f);
    CHECK(near(after_right_drag.y, 0.0f, 1e-3f));

    // Dragging down must look down (not inverted).
    game::DebugCamera pitch_camera;
    pitch_camera.set_position(Vec3::zero(), Vec3::forward());
    Input pitch_input;
    SDL_Event down_event = {};
    down_event.type = SDL_EVENT_MOUSE_MOTION;
    down_event.motion.xrel = 0.0f;
    down_event.motion.yrel = 100.0f;
    pitch_input.begin_frame();
    pitch_input.handle_event(down_event, false);
    pitch_camera.update(pitch_input, 1.0f / 60.0f, true);
    std::printf("  drag down  -> fwd.y %.3f\n", pitch_camera.camera().forward().y);
    CHECK(pitch_camera.camera().forward().y < 0.0f);
}

void test_pitch_clamp() {
    std::printf("pitch clamp\n");
    // Enough drag to blow past vertical many times over. Pitch must clamp, and
    // yaw must stay meaningful rather than flipping the view upside down.
    game::DebugCamera camera;
    camera.set_position(Vec3::zero(), Vec3::forward());
    Input input;
    for (int i = 0; i < 40; ++i) {
        SDL_Event event = {};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.yrel = -100.0f;  // drag up, hard
        input.begin_frame();
        input.handle_event(event, false);
        camera.update(input, 1.0f / 60.0f, true);
    }
    Vec3 forward = camera.camera().forward();
    std::printf("  after hard up-drag: fwd (%.3f %.3f %.3f)\n", forward.x, forward.y, forward.z);
    CHECK(forward.y > 0.99f);           // pinned near straight up
    CHECK(forward.y < 1.0f);            // but never exactly vertical
    CHECK(camera.camera().up().y > 0.0f);  // never flipped over
}

void test_movement() {
    std::printf("movement\n");
    // W must move along the camera's forward axis, and releasing it must bring
    // the camera to rest rather than letting it drift.
    game::DebugCamera camera;
    camera.set_position(Vec3::zero(), Vec3::forward());

    Input input;
    SDL_Event key = {};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_W;
    input.begin_frame();
    input.handle_event(key, false);

    for (int i = 0; i < 60; ++i) camera.update(input, 1.0f / 60.0f, false);
    Vec3 travelled = camera.camera().position;
    std::printf("  1s of W: (%.2f %.2f %.2f)\n", travelled.x, travelled.y, travelled.z);
    CHECK(travelled.z < -1.0f);                      // moved forward, i.e. -Z
    CHECK(near(travelled.x, 0.0f, 1e-3f));           // no lateral drift
    CHECK(near(travelled.y, 0.0f, 1e-3f));

    // Release W and let it settle.
    SDL_Event key_up = {};
    key_up.type = SDL_EVENT_KEY_UP;
    key_up.key.scancode = SDL_SCANCODE_W;
    input.begin_frame();
    input.handle_event(key_up, false);
    Vec3 at_release = camera.camera().position;
    for (int i = 0; i < 180; ++i) camera.update(input, 1.0f / 60.0f, false);
    float coast = length(camera.camera().position - at_release);
    std::printf("  coast after release: %.3f m\n", coast);
    CHECK(coast < 2.0f);  // smoothing settles instead of drifting forever
}

}  // namespace

int main() {
    test_set_position();
    test_mouse_look();
    test_pitch_clamp();
    test_movement();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

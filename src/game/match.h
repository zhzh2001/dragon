#pragma once

namespace game {

struct CombatEvents;

// The match loop: countdown, fight, results, rematch.
//
// Pure scorekeeping and phase logic -- it consumes CombatEvents and emits
// nothing but state, so the whole loop is testable without an app. Deathmatch
// rules: the player scores by killing hostiles, the hostiles score by killing
// the player, first to the target wins; if the clock runs out, the leader wins.
enum class MatchPhase : int {
    Idle,       // free play, no scoring
    Countdown,  // fight staged, weapons cold
    Fighting,
    Results,
};

struct MatchSettings {
    int target_kills = 5;
    // 0 disables the clock.
    float time_limit = 240.0f;
    float countdown = 3.0f;
};

class Match {
public:
    MatchSettings settings;

    void start();
    // Back to free play, clearing any result.
    void abandon();

    // `events` is the frame's combat outcome; kills and deaths accumulate only
    // while fighting.
    void update(float dt, const CombatEvents& events);

    MatchPhase phase() const { return phase_; }
    bool scoring() const { return phase_ == MatchPhase::Fighting; }
    // Weapons are live outside matches (free play) and during the fight, cold
    // in the countdown and on the results screen.
    bool weapons_live() const {
        return phase_ == MatchPhase::Idle || phase_ == MatchPhase::Fighting;
    }

    int player_kills() const { return player_kills_; }
    int player_deaths() const { return player_deaths_; }
    // Seconds until the fight starts / until the clock expires.
    float countdown_remaining() const { return countdown_remaining_; }
    float time_remaining() const { return time_remaining_; }
    float fight_duration() const { return fight_time_; }

    // Valid in Results.
    bool player_won() const { return player_won_; }
    bool draw() const { return draw_; }

private:
    void finish(bool player_won, bool draw);

    MatchPhase phase_ = MatchPhase::Idle;
    int player_kills_ = 0;
    int player_deaths_ = 0;
    float countdown_remaining_ = 0.0f;
    float time_remaining_ = 0.0f;
    float fight_time_ = 0.0f;
    bool player_won_ = false;
    bool draw_ = false;
};

}  // namespace game

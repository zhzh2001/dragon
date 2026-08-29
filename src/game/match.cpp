#include "game/match.h"

#include "core/math.h"
#include "game/combat.h"

namespace game {

void Match::start() {
    phase_ = MatchPhase::Countdown;
    player_kills_ = 0;
    player_deaths_ = 0;
    countdown_remaining_ = core::maxf(settings.countdown, 0.0f);
    time_remaining_ = settings.time_limit;
    fight_time_ = 0.0f;
    player_won_ = false;
    draw_ = false;
    if (countdown_remaining_ <= 0.0f) phase_ = MatchPhase::Fighting;
}

void Match::abandon() {
    phase_ = MatchPhase::Idle;
}

void Match::finish(bool player_won, bool draw) {
    phase_ = MatchPhase::Results;
    player_won_ = player_won;
    draw_ = draw;
}

void Match::update(float dt, const CombatEvents& events) {
    switch (phase_) {
        case MatchPhase::Idle:
        case MatchPhase::Results:
            return;
        case MatchPhase::Countdown:
            countdown_remaining_ -= dt;
            if (countdown_remaining_ <= 0.0f) {
                countdown_remaining_ = 0.0f;
                phase_ = MatchPhase::Fighting;
            }
            return;
        case MatchPhase::Fighting:
            break;
    }

    fight_time_ += dt;
    player_kills_ += events.kills;
    if (events.player_died) ++player_deaths_;

    if (player_kills_ >= settings.target_kills) {
        finish(true, false);
        return;
    }
    if (player_deaths_ >= settings.target_kills) {
        finish(false, false);
        return;
    }

    if (settings.time_limit > 0.0f) {
        time_remaining_ -= dt;
        if (time_remaining_ <= 0.0f) {
            time_remaining_ = 0.0f;
            // The clock favours whoever is ahead; a tie is a draw, honestly.
            if (player_kills_ == player_deaths_) {
                finish(false, true);
            } else {
                finish(player_kills_ > player_deaths_, false);
            }
        }
    }
}

}  // namespace game

#include "bot.h"

Action RandomBot::act(const PlayerView& v) {
    int roll = rng.below(10);
    if (roll < 2 && v.to_call > 0) return {FOLD};
    if (roll >= 8 && v.can_raise) {
        // mostly small raises, sometimes all-in
        if (rng.below(10) == 0) return {RAISE, v.max_raise_to};
        int steps = (v.max_raise_to - v.min_raise_to) / BIG_BLIND;
        return {RAISE, v.min_raise_to + BIG_BLIND * (int)rng.below(min(steps, 5) + 1)};
    }
    return {v.to_call > 0 ? CALL : CHECK};
}

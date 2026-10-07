#include "bots/bot.h"
#include "engine/evaluator.h"

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

Action CallBot::act(const PlayerView& v) {
    return {v.to_call > 0 ? CALL : CHECK};
}

Action EquityBot::act(const PlayerView& v) {
    int opponents = 0;
    for (int s = 0; s < (int)v.folded.size(); s++) {
        if (s != v.id && !v.folded[s]) opponents++;
    }
    float eq = fx.equity(v.hole, v.community, opponents);
    // equity a hand needs to be "strong" here: a fair share of the pot, plus a margin
    float strong = min(0.85f, 1.0f / (opponents + 1) + 0.25f);
    int pot_raise = v.current_bet + v.pot + v.to_call; // raise the size of the pot

    if (eq > strong && v.can_raise) {
        return {RAISE, max(v.min_raise_to, min(v.max_raise_to, pot_raise))};
    }
    if (v.to_call == 0) return {CHECK};
    float pot_odds = (float)v.to_call / (v.pot + v.to_call);
    return {eq > pot_odds + 0.05f ? CALL : FOLD};
}

Action TightAggressiveBot::act(const PlayerView& v) {
    auto raise_to = [&](float pot_fraction) {
        int to = v.current_bet + (int)((v.pot + v.to_call) * pot_fraction);
        return Action{RAISE, max(v.min_raise_to, min(v.max_raise_to, to))};
    };
    Action check_or_fold = {v.to_call == 0 ? CHECK : FOLD};

    if (v.community.empty()) {
        float chen = chen_score(v.hole[0], v.hole[1]);
        if (chen >= 10 && v.can_raise) return raise_to(1.0f);                         // premium: raise
        if (chen >= 10) return {v.to_call ? CALL : CHECK};
        if (chen >= 7 && v.to_call <= 3 * BIG_BLIND) return {v.to_call ? CALL : CHECK}; // playable: cheap calls only
        return check_or_fold;
    }

    Hand_rankings made = score_category(evaluate(v.hole, v.community));
    // count pairs that use the board only as a weaker hand
    Hand_rankings board_only = v.community.size() >= 5 ? score_category(evaluate({}, v.community)) : HIGH_CARD;
    bool real_hand = made > board_only;

    if (real_hand && made >= TWO_PAIR) {
        return v.can_raise ? raise_to(1.0f) : Action{v.to_call ? CALL : CHECK};
    }
    if (real_hand && made == PAIR) {
        if (v.to_call == 0) return v.can_raise && rng.below(2) ? raise_to(0.5f) : Action{CHECK};
        return {v.to_call * 2 <= v.pot ? CALL : FOLD};
    }
    bool drawing = has_flush_draw(v.hole, v.community) || straight_outs(v.hole, v.community) >= 2;
    if (drawing && v.to_call > 0) {
        float pot_odds = (float)v.to_call / (v.pot + v.to_call);
        return {pot_odds < 0.25f ? CALL : FOLD};
    }
    if (v.to_call == 0 && v.can_raise && rng.below(10) == 0) return raise_to(0.5f); // occasional bluff
    return check_or_fold;
}

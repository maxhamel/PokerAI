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

static float uniform(FastRng& rng, float lo, float hi) {
    return lo + (hi - lo) * (rng() / 4294967296.0f);
}

Style Style::tight(FastRng& rng) {
    Style s;
    s.call_margin = uniform(rng, 0.05f, 0.20f);
    s.value_margin = uniform(rng, 0.15f, 0.35f);
    s.aggression = uniform(rng, 0.5f, 1.0f);
    s.bluff = uniform(rng, 0.0f, 0.08f);
    s.bet_size = uniform(rng, 0.5f, 1.2f);
    return s;
}

Style Style::loose_aggressive(FastRng& rng) {
    Style s;
    s.call_margin = uniform(rng, -0.15f, 0.0f);
    s.value_margin = uniform(rng, 0.0f, 0.12f);
    s.aggression = uniform(rng, 0.6f, 1.0f);
    s.bluff = uniform(rng, 0.2f, 0.45f);
    s.bet_size = uniform(rng, 0.6f, 1.5f);
    return s;
}

Style Style::random(FastRng& rng) {
    auto uniform = [&](float lo, float hi) { return ::uniform(rng, lo, hi); };
    Style s;
    s.call_margin = uniform(-0.15f, 0.20f);
    s.value_margin = uniform(0.0f, 0.35f);
    s.aggression = uniform(0.2f, 1.0f);
    s.bluff = uniform(0.0f, 0.4f);
    s.bet_size = uniform(0.4f, 1.5f);
    return s;
}

Action StyleBot::act(const PlayerView& v) {
    int opponents = 0;
    for (int s = 0; s < (int)v.folded.size(); s++) {
        if (s != v.id && !v.folded[s]) opponents++;
    }
    float eq = fx.equity(v.hole, v.community, opponents);
    float fair_share = 1.0f / (opponents + 1);
    float chance = rng() / 4294967296.0f;
    int to = v.current_bet + (int)((v.pot + v.to_call) * style.bet_size);
    Action bet = {RAISE, max(v.min_raise_to, min(v.max_raise_to, to))};

    if (eq > fair_share + style.value_margin && v.can_raise && chance < style.aggression) return bet;
    if (v.to_call == 0) {
        return v.can_raise && chance < style.bluff ? bet : Action{CHECK};
    }
    float pot_odds = (float)v.to_call / (v.pot + v.to_call);
    return {eq > pot_odds + style.call_margin ? CALL : FOLD};
}

Action LooseAggressiveBot::act(const PlayerView& v) {
    auto raise_to = [&](float pot_fraction) {
        int to = v.current_bet + (int)((v.pot + v.to_call) * pot_fraction);
        return Action{RAISE, max(v.min_raise_to, min(v.max_raise_to, to))};
    };
    Action call_or_check = {v.to_call ? CALL : CHECK};
    Action check_or_fold = {v.to_call ? FOLD : CHECK};

    if (v.community.empty()) {
        float chen = chen_score(v.hole[0], v.hole[1]);
        if (chen >= 7 && v.can_raise && rng.below(10) < 7) return raise_to(0.75f);       // raise most decent hands
        if (chen >= 3 && v.to_call <= 10 * BIG_BLIND) return call_or_check;               // call almost anything cheap
        if (chen >= 5) return call_or_check;
        return check_or_fold;
    }

    Hand_rankings made = score_category(evaluate(v.hole, v.community));
    Hand_rankings board_only = v.community.size() >= 5 ? score_category(evaluate({}, v.community)) : HIGH_CARD;
    bool real_hand = made > board_only;
    bool drawing = has_flush_draw(v.hole, v.community) || straight_outs(v.hole, v.community) >= 1;

    if (real_hand && made >= TWO_PAIR) return v.can_raise ? raise_to(1.0f) : call_or_check;
    if ((real_hand || drawing) && v.can_raise && rng.below(10) < 6) return raise_to(0.66f); // bet any pair or draw
    if (real_hand) return call_or_check;                                                     // call down with any pair
    if (drawing && v.to_call * 3 <= v.pot) return call_or_check;
    if (v.to_call == 0 && v.can_raise && rng.below(100) < 35) return raise_to(0.5f);      // frequent bluffs
    return check_or_fold;
}

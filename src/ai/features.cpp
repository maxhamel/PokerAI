#include "ai/features.h"
#include "engine/evaluator.h"
#include <cmath>

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

// Maps [0, inf) to [0, 1) smoothly, so large values don't need a cap.
static float squash(float x) { return x / (1 + x); }

// Bit r set = rank r present. An ace also counts as low for straights.
static bool has_straight(unsigned mask) {
    unsigned m = (mask << 1) | (mask >> ACE & 1);
    return m & (m >> 1) & (m >> 2) & (m >> 3) & (m >> 4);
}

static unsigned rank_mask(span<const Card> hole, span<const Card> board) {
    unsigned mask = 0;
    for (span<const Card> cards : {hole, board}) {
        for (const Card& c : cards) mask |= 1u << c.rank;
    }
    return mask;
}

float chen_score(const Card& a, const Card& b) {
    auto points = [](Rank r) -> float {
        switch (r) {
            case ACE: return 10;
            case KING: return 8;
            case QUEEN: return 7;
            case JACK: return 6;
            default: return (r + 2) / 2.0f; // 10 -> 5, 9 -> 4.5, ..., 2 -> 1
        }
    };
    Rank high = max(a.rank, b.rank), low = min(a.rank, b.rank);
    float score = points(high);
    if (high == low) {
        score = max(5.0f, score * 2); // pairs
    } else {
        if (a.suit == b.suit) score += 2;
        int gap = high - low - 1;
        score -= gap == 0 ? 0 : gap == 1 ? 1 : gap == 2 ? 2 : gap == 3 ? 4 : 5;
        if (gap <= 1 && high < QUEEN) score += 1; // connected small cards
    }
    return ceilf(score);
}

bool has_flush_draw(span<const Card> hole, span<const Card> board) {
    if (board.size() >= 5) return false; // no cards to come
    int suit_count[4] = {};
    for (span<const Card> cards : {hole, board}) {
        for (const Card& c : cards) suit_count[c.suit]++;
    }
    bool four = false;
    for (int s = 0; s < 4; s++) {
        if (suit_count[s] >= 5) return false; // already a flush
        if (suit_count[s] == 4) four = true;
    }
    return four;
}

int straight_outs(span<const Card> hole, span<const Card> board) {
    if (board.size() >= 5) return 0;
    unsigned mask = rank_mask(hole, board);
    if (has_straight(mask)) return 0;
    int outs = 0;
    for (int r = TWO; r <= ACE; r++) {
        if (!(mask >> r & 1) && has_straight(mask | 1u << r)) outs++;
    }
    return outs;
}

FeatureExtractor::FeatureExtractor(uint64_t seed, int equity_samples)
    : rng(seed), equity_samples(max(1, equity_samples)) {}

float FeatureExtractor::equity(span<const Card> hole, span<const Card> board, int opponents) {
    if (opponents <= 0) return 1;

    // every card we can't see
    bool seen[52] = {};
    for (span<const Card> cards : {hole, board}) {
        for (const Card& c : cards) seen[c.suit * 13 + c.rank] = true;
    }
    Card unknown[52];
    int n_unknown = 0;
    for (int i = 0; i < 52; i++) {
        if (!seen[i]) unknown[n_unknown++] = Card(static_cast<Suit>(i / 13), static_cast<Rank>(i % 13));
    }

    int board_needed = 5 - (int)board.size();
    int needed = board_needed + 2 * opponents;
    Card full_board[5];
    copy(board.begin(), board.end(), full_board);

    double wins = 0;
    for (int s = 0; s < equity_samples; s++) {
        // deal the missing board cards and the opponents' hands (partial shuffle)
        for (int k = 0; k < needed; k++) {
            swap(unknown[k], unknown[k + rng.below(n_unknown - k)]);
        }
        copy(unknown, unknown + board_needed, full_board + board.size());

        int mine = evaluate(hole, span<const Card>(full_board, 5));
        int ties = 0;
        bool lost = false;
        for (int o = 0; o < opponents && !lost; o++) {
            int theirs = evaluate(span<const Card>(unknown + board_needed + 2 * o, 2),
                                  span<const Card>(full_board, 5));
            if (theirs > mine) lost = true;
            else if (theirs == mine) ties++;
        }
        if (!lost) wins += 1.0 / (ties + 1);
    }
    return wins / equity_samples;
}

void FeatureExtractor::extract(const PlayerView& v, Features& out) {
    out.fill(0);
    int n = v.stacks.size();
    int me = v.id;
    span<const Card> hole = v.hole, board = v.community;
    const float bb = BIG_BLIND;

    int live_opponents = 0, active_opponents = 0;
    for (int s = 0; s < n; s++) {
        if (s == me || v.folded[s]) continue;
        live_opponents++;
        if (v.stacks[s] > 0) active_opponents++;
    }

    // hand strength
    out[F_EQUITY] = equity(hole, board, live_opponents);
    out[F_CATEGORY + score_category(evaluate(hole, board)) - HIGH_CARD] = 1;
    out[F_FLUSH_DRAW] = has_flush_draw(hole, board);
    out[F_STRAIGHT_DRAW] = min(straight_outs(hole, board), 2) / 2.0f;
    if (hole.size() == 2) out[F_PREFLOP] = clamp01((chen_score(hole[0], hole[1]) + 1) / 21);

    out[F_STREET + v.street] = 1;

    // price
    int stack = v.stacks[me];
    out[F_POT_ODDS] = v.to_call > 0 ? (float)v.to_call / (v.pot + v.to_call) : 0;
    out[F_STACK_TO_POT] = v.pot > 0 ? squash((float)stack / v.pot) : 1;
    out[F_STACK] = clamp01(stack / bb / 200);
    out[F_CALL_TO_STACK] = stack > 0 ? clamp01((float)v.to_call / stack) : 1;
    out[F_POT] = clamp01(v.pot / bb / 200);

    // position: live opponents between us and the button act after us postflop
    int after = 0;
    if (me != v.button) {
        for (int k = 1; k < n; k++) {
            int s = (me + k) % n;
            if (!v.folded[s]) after++;
            if (s == v.button) break;
        }
    }
    out[F_ACTS_AFTER] = live_opponents > 0 ? (float)after / live_opponents : 0;
    out[F_LIVE_OPPONENTS] = (float)live_opponents / MAX_OPPONENTS;
    out[F_ACTIVE_OPPONENTS] = (float)active_opponents / MAX_OPPONENTS;

    // betting history
    out[F_RAISES_THIS_STREET] = clamp01(v.raises[v.street] / 4.0f);
    out[F_RAISES_LAST_STREET] = v.street > PREFLOP ? clamp01(v.raises[v.street - 1] / 4.0f) : 0;
    out[F_I_AM_AGGRESSOR] = v.last_aggressor == me;
    out[F_OTHER_AGGRESSOR] = v.last_aggressor != -1 && v.last_aggressor != me;

    // opponents, clockwise starting with the player to our left
    for (int k = 1; k < n && k <= MAX_OPPONENTS; k++) {
        int s = (me + k) % n;
        float* f = &out[F_OPPONENTS + (k - 1) * OPPONENT_FEATURES];
        f[0] = !v.folded[s];
        f[1] = !v.folded[s] && v.stacks[s] == 0;
        f[2] = clamp01(v.stacks[s] / bb / 200);
        f[3] = v.pot > 0 ? clamp01((float)v.street_bets[s] / v.pot) : 0;
    }
}

string FeatureExtractor::feature_name(int i) {
    static const char* categories[] = {"high card", "pair", "two pair", "trips", "straight",
                                       "flush", "full house", "quads", "straight flush"};
    static const char* streets[] = {"preflop", "flop", "turn", "river"};
    static const char* opponent[] = {"in hand", "all-in", "stack", "street bet"};
    if (i >= F_CATEGORY && i < F_CATEGORY + 9) return string("category: ") + categories[i - F_CATEGORY];
    if (i >= F_STREET && i < F_STREET + 4) return string("street: ") + streets[i - F_STREET];
    if (i >= F_OPPONENTS) {
        int k = (i - F_OPPONENTS) / OPPONENT_FEATURES;
        return "opponent " + to_string(k + 1) + ": " + opponent[(i - F_OPPONENTS) % OPPONENT_FEATURES];
    }
    switch (i) {
        case F_EQUITY: return "equity";
        case F_FLUSH_DRAW: return "flush draw";
        case F_STRAIGHT_DRAW: return "straight draw";
        case F_PREFLOP: return "preflop strength";
        case F_POT_ODDS: return "pot odds";
        case F_STACK_TO_POT: return "stack to pot";
        case F_STACK: return "stack";
        case F_CALL_TO_STACK: return "call to stack";
        case F_POT: return "pot";
        case F_ACTS_AFTER: return "opponents acting after";
        case F_LIVE_OPPONENTS: return "live opponents";
        case F_ACTIVE_OPPONENTS: return "active opponents";
        case F_RAISES_THIS_STREET: return "raises this street";
        case F_RAISES_LAST_STREET: return "raises last street";
        case F_I_AM_AGGRESSOR: return "I am aggressor";
        case F_OTHER_AGGRESSOR: return "other aggressor";
    }
    return "?";
}

#pragma once

#include "engine/types.h"
#include "engine/rng.h"
#include <span>

// Turns a PlayerView into a fixed-size vector of numbers for a neural
// network. Every feature is in [0, 1], and everything is relative to the
// acting player (opponents are listed clockwise from them, chips are measured
// in big blinds or as a fraction of the pot), so the network doesn't have to
// relearn the same thing for every seat.
//
// If you change the layout, bump FEATURE_VERSION: networks trained on an old
// layout won't work with a new one.

constexpr int FEATURE_VERSION = 1;
constexpr int MAX_OPPONENTS = PLAYER_COUNT - 1;
constexpr int OPPONENT_FEATURES = 4;

// Index of each feature in the vector.
enum Feature {
    // hand strength
    F_EQUITY,                       // chance to win vs. the live opponents (Monte Carlo)
    F_CATEGORY,                     // one-hot, HIGH_CARD .. STRAIGHT_FLUSH (9 entries)
    F_FLUSH_DRAW = F_CATEGORY + 9,  // four to a flush with cards still to come
    F_STRAIGHT_DRAW,                // 0.5 = gutshot, 1 = open-ended (or better)
    F_PREFLOP,                      // Chen formula score of the hole cards, scaled
    // street
    F_STREET,                       // one-hot, PREFLOP .. RIVER (4 entries)
    // price
    F_POT_ODDS = F_STREET + 4,      // to_call / (pot + to_call)
    F_STACK_TO_POT,                 // squashed: spr / (1 + spr)
    F_STACK,                        // own stack in big blinds, / 200, capped
    F_CALL_TO_STACK,                // to_call / own stack, capped (1 = calling puts us all-in)
    F_POT,                          // pot in big blinds, / 200, capped
    // position
    F_ACTS_AFTER,                   // share of live opponents acting after us postflop (0 = button)
    F_LIVE_OPPONENTS,               // opponents still in the hand / MAX_OPPONENTS
    F_ACTIVE_OPPONENTS,             // opponents who can still bet (not all-in) / MAX_OPPONENTS
    // betting history
    F_RAISES_THIS_STREET,           // / 4, capped
    F_RAISES_LAST_STREET,           // / 4, capped (0 preflop)
    F_I_AM_AGGRESSOR,               // we made the last bet/raise this hand
    F_OTHER_AGGRESSOR,              // someone else did
    // opponents, clockwise from us: in hand, all-in, stack / 200 BB, street bet / pot
    F_OPPONENTS,
    FEATURE_COUNT = F_OPPONENTS + MAX_OPPONENTS * OPPONENT_FEATURES
};

using Features = array<float, FEATURE_COUNT>;

class FeatureExtractor {
    private:
        FastRng rng;
        int equity_samples;

    public:
        // More equity samples = more accurate equity, but slower (each sample
        // evaluates one hand per player still in).
        explicit FeatureExtractor(uint64_t seed, int equity_samples = 200);

        // Fills `out` for the acting player in `v` (v.id must be a real seat).
        void extract(const PlayerView& v, Features& out);

        // Chance that `hole` wins at showdown against `opponents` random
        // hands, with the rest of the board dealt at random. Ties count as a
        // share of the win.
        float equity(span<const Card> hole, span<const Card> board, int opponents);

        static string feature_name(int i);
};

// Chen formula strength of two hole cards: -1 (7-2 offsuit) to 20 (aces).
float chen_score(const Card& a, const Card& b);

// Draws in the cards so far: whether there's a four-card flush draw, and the
// number of different ranks that would complete a straight (2 = open-ended).
// Both are 0 once a flush/straight is already made.
bool has_flush_draw(span<const Card> hole, span<const Card> board);
int straight_outs(span<const Card> hole, span<const Card> board);

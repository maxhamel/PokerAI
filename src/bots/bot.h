#pragma once

#include "engine/agent.h"
#include "engine/rng.h"
#include "ai/features.h"

// Picks a random legal action. A baseline to test against until the real AI
// exists.
class RandomBot : public Agent {
    private:
        FastRng rng;

    public:
        explicit RandomBot(unsigned seed) : rng(seed) {}
        Action act(const PlayerView& v) override;
};

// Always checks or calls. A "calling station": loses to anyone who bets their
// good hands and doesn't bluff into it.
class CallBot : public Agent {
    public:
        Action act(const PlayerView& v) override;
};

// A simple rule-based player: estimates its equity against the players still
// in, then bets strong hands, calls when the pot odds are good enough, and
// folds the rest. A reasonable yardstick for a trained AI.
class EquityBot : public Agent {
    private:
        FeatureExtractor fx;

    public:
        explicit EquityBot(uint64_t seed, int equity_samples = 50) : fx(seed, equity_samples) {}
        Action act(const PlayerView& v) override;
};

// A tight-aggressive rule player with a different style from EquityBot: it
// picks starting hands by Chen score, bets made hands, chases draws only at a
// good price, and sometimes bluffs. Never used in training, so it's a fair
// test of whether a trained network learned poker or just learned to beat
// the bots it trained against.
class TightAggressiveBot : public Agent {
    private:
        FastRng rng;

    public:
        explicit TightAggressiveBot(uint64_t seed) : rng(seed) {}
        Action act(const PlayerView& v) override;
};

// How a StyleBot plays. Randomizing these gives a spread of opponents from
// tight to loose and passive to aggressive.
struct Style {
    float call_margin;  // calls when equity > pot odds + this. Negative = loose (calls too much)
    float value_margin; // bets/raises when equity > fair share + this. Small = bets thinner hands
    float aggression;   // chance of actually betting/raising a hand that qualifies (else just calls)
    float bluff;        // chance of betting with a weak hand when checked to
    float bet_size;     // bets and raises, as a fraction of the pot

    // A random style. Ranges cover calling stations to rocks, and passive
    // to maniac.
    static Style random(FastRng& rng);

    // Random styles from two corners of that range: tight (plays few
    // hands, rarely bluffs) and loose-aggressive (plays many hands, bets
    // thin and bluffs often).
    static Style tight(FastRng& rng);
    static Style loose_aggressive(FastRng& rng);
};

// An equity-based player whose style is set by a Style. Used as the training
// opponents, so the AI sees many styles instead of one.
class StyleBot : public Agent {
    private:
        Style style;
        FeatureExtractor fx;
        FastRng rng;

    public:
        StyleBot(const Style& style, uint64_t seed, int equity_samples = 50)
            : style(style), fx(seed, equity_samples), rng(seed ^ 0x9e3779b9) {}
        Action act(const PlayerView& v) override;
};

// A loose-aggressive rule player: plays most hands, raises often, bets any
// pair or draw, bluffs a lot, and calls down with any pair. Like
// TightAggressiveBot it's hand-written (no equity) and never used in
// training: the second held-out test opponent.
class LooseAggressiveBot : public Agent {
    private:
        FastRng rng;

    public:
        explicit LooseAggressiveBot(uint64_t seed) : rng(seed) {}
        Action act(const PlayerView& v) override;
};

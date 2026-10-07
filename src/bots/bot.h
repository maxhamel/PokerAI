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

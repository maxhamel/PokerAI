#pragma once

#include "ai/neural.h"
#include <thread>

// Average profit per hand with a 95% confidence interval.
struct Score {
    double chips_per_hand = 0;
    double ci95 = 0;   // the true value is within +-ci95 with 95% confidence
    long hands = 0;

    double bb_per_100() const { return chips_per_hand / BIG_BLIND * 100; }
};

// Opponent tables for evaluation. The StyleBot tables use a random style
// per seat, drawn from all styles, the tight range, or the loose-aggressive
// range. VS_TIGHT_AGGRESSIVE and VS_LOOSE_AGGRESSIVE are hand-written bots
// that are never used in training: they're the held-out test.
enum Baseline {
    VS_RANDOM, VS_CALL, VS_EQUITY, VS_STYLES, VS_TIGHT_STYLES, VS_LOOSE_STYLES,
    VS_TIGHT_AGGRESSIVE, VS_LOOSE_AGGRESSIVE
};
string baseline_name(Baseline b);

struct TrainConfig {
    int players = 6;            // table size: 6 for 6-max, 2 for heads-up
    int generations = 100;
    int population = 48;        // networks per generation
    int hands = 1600;           // cash hands each network plays per generation (its fitness)
    int block = 50;             // hands before the opponents at the table change
    int elites = 6;             // best networks copied unchanged into the next generation
    float mutation_rate = 0.1f; // chance each weight is changed in a child
    float mutation_size = 0.1f; // standard deviation of a change
    int equity_samples = 32;    // per decision while training (fewer = faster, noisier)
    float temperature = 0.25f;  // action randomness of the networks being trained
    // Training opponents: each seat is drawn with these relative weights.
    // "versions" are earlier versions of the network being trained (from
    // the pool); "tight" and "loose" are StyleBots with tight or
    // loose-aggressive styles.
    int mix_versions = 40, mix_tight = 20, mix_loose = 20, mix_random = 20;
    int mix_equity = 0, mix_call = 0;
    // Pool of past versions. Every eval_every generations the champion
    // plays pool_hands hands against each version; it joins the pool only
    // if it beats the pool overall with 95% confidence, replacing the oldest
    // version when the pool is full.
    int pool_size = 8;
    int pool_hands = 3000;
    int stack = 200;            // chips at the start of every hand (100 big blinds)
    int eval_every = 10;        // generations between pool checks
    int eval_hands = 20000;     // hands for the validation printout (fixed bots from the mix)
    uint64_t seed = 1;
    int threads = max(1u, thread::hardware_concurrency());
    string out_path;            // newest pool version is saved here; the pool goes in
                                // <out_path without .net>_versions/
};

// Evolves a network that maximizes chips won per hand, playing against a
// mix of fixed bots and its own past versions. Prints progress, saves each
// version that joins the pool, and returns the newest one.
Network train(const TrainConfig& cfg);

// Plays `net` in seat 1 against a table of baseline bots for `hands` cash
// hands (split across threads) and reports its profit per hand.
Score evaluate(const Network& net, Baseline opponents, int players, int hands, int stack,
               uint64_t seed, int threads, int equity_samples = 50);

// Same, against a table full of copies of another network.
Score evaluate_vs(const Network& net, const Network& opponent, int players, int hands, int stack,
                  uint64_t seed, int threads, int equity_samples = 50);

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

// VS_TIGHT_AGGRESSIVE is never used in training: it's the held-out test.
enum Baseline { VS_RANDOM, VS_CALL, VS_EQUITY, VS_TIGHT_AGGRESSIVE };
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
    int self_play = 0;          // % of opponents that are other networks (the rest are baseline bots)
    int stack = 200;            // chips at the start of every hand (100 big blinds)
    int eval_every = 10;        // generations between checks against the baseline bots
    int eval_hands = 20000;     // hands per check
    uint64_t seed = 1;
    int threads = max(1u, thread::hardware_concurrency());
    string out_path;            // where to save the best network found
};

// Evolves a network that maximizes chips won per hand. Prints progress, saves
// the best network (by its score against EquityBots) to cfg.out_path, and
// returns it.
Network train(const TrainConfig& cfg);

// Plays `net` in seat 1 against a table of baseline bots for `hands` cash
// hands (split across threads) and reports its profit per hand.
Score evaluate(const Network& net, Baseline opponents, int players, int hands, int stack,
               uint64_t seed, int threads, int equity_samples = 50);

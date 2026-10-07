#include "ai/train.h"
#include "bots/bot.h"
#include "engine/game.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>

string baseline_name(Baseline b) {
    return b == VS_RANDOM ? "RandomBot" : b == VS_CALL ? "CallBot"
         : b == VS_EQUITY ? "EquityBot" : "TightAggressiveBot";
}

// Runs f(0) .. f(n - 1) across `threads` threads.
template <class F>
static void parallel_for(int n, int threads, F f) {
    atomic<int> next{0};
    auto worker = [&] {
        for (int i; (i = next++) < n;) f(i);
    };
    vector<thread> pool;
    for (int t = 1; t < min(threads, n); t++) pool.emplace_back(worker);
    worker();
    for (thread& t : pool) t.join();
}

// Running sum of per-hand profits, for an average and confidence interval.
struct Tally {
    double sum = 0, sum_sq = 0;
    long hands = 0;

    void add(double x) { sum += x; sum_sq += x * x; hands++; }
    void add(const Tally& o) { sum += o.sum; sum_sq += o.sum_sq; hands += o.hands; }

    Score score() const {
        Score s;
        s.hands = hands;
        if (hands == 0) return s;
        s.chips_per_hand = sum / hands;
        double variance = max(0.0, sum_sq / hands - s.chips_per_hand * s.chips_per_hand);
        s.ci95 = 1.96 * sqrt(variance / hands);
        return s;
    }
};

// Who sits at the table with the network being tested.
enum OpponentKind { OPP_RANDOM, OPP_CALL, OPP_EQUITY, OPP_TIGHT_AGGRESSIVE, OPP_HALL_OF_FAME, OPP_POPULATION };

struct OpponentSpec {
    OpponentKind kind;
    int index;      // which hall-of-fame or population network
    uint64_t seed;
};

static unique_ptr<Agent> make_opponent(const OpponentSpec& s, const vector<Network>& hall,
                                       const vector<Network>& population, int equity_samples) {
    switch (s.kind) {
        case OPP_RANDOM: return make_unique<RandomBot>(s.seed);
        case OPP_CALL: return make_unique<CallBot>();
        case OPP_EQUITY: return make_unique<EquityBot>(s.seed, equity_samples);
        case OPP_TIGHT_AGGRESSIVE: return make_unique<TightAggressiveBot>(s.seed);
        case OPP_HALL_OF_FAME: return make_unique<NeuralBot>(&hall[s.index], s.seed, equity_samples);
        case OPP_POPULATION: return make_unique<NeuralBot>(&population[s.index], s.seed, equity_samples);
    }
    return nullptr;
}

// Plays `hero` in seat 0 against `opponents` for `hands` cash hands.
static Tally play_hands(Agent& hero, const vector<Agent*>& opponents, int hands, int stack, uint64_t deck_seed) {
    vector<Agent*> seats = {&hero};
    seats.insert(seats.end(), opponents.begin(), opponents.end());
    Game game(seats, nullptr, false, deck_seed);
    Tally t;
    for (int h = 0; h < hands; h++) t.add(game.play_cash_hand(stack)[0]);
    return t;
}

Score evaluate(const Network& net, Baseline opponents, int players, int hands, int stack,
               uint64_t seed, int threads, int equity_samples) {
    const int chunk = 500;
    int chunks = (hands + chunk - 1) / chunk;
    vector<Tally> tallies(chunks);
    parallel_for(chunks, threads, [&](int c) {
        uint64_t s = seed * 1000003 + c * 7919;
        NeuralBot hero(&net, s, equity_samples);
        vector<unique_ptr<Agent>> owned;
        vector<Agent*> opps;
        for (int i = 1; i < players; i++) {
            OpponentKind kind = opponents == VS_RANDOM ? OPP_RANDOM : opponents == VS_CALL ? OPP_CALL
                              : opponents == VS_EQUITY ? OPP_EQUITY : OPP_TIGHT_AGGRESSIVE;
            owned.push_back(make_opponent({kind, 0, s + i}, {}, {}, equity_samples));
            opps.push_back(owned.back().get());
        }
        tallies[c] = play_hands(hero, opps, min(chunk, hands - c * chunk), stack, s);
    });
    Tally total;
    for (const Tally& t : tallies) total.add(t);
    return total.score();
}

static Network mutate(const Network& parent, FastRng& rng, const TrainConfig& cfg) {
    Network child = parent;
    normal_distribution<float> normal(0.0f, cfg.mutation_size);
    for (float& w : child.w) {
        if (rng.below(1000000) < cfg.mutation_rate * 1000000) w += normal(rng);
    }
    return child;
}

Network train(const TrainConfig& cfg) {
    FastRng rng(cfg.seed);
    vector<Network> population;
    for (int i = 0; i < cfg.population; i++) {
        population.push_back(Network::random(rng, cfg.players));
        population.back().temperature = cfg.temperature;
    }
    vector<Network> hall; // champions of past generations
    const int HALL_SIZE = 20;

    int blocks = max(1, (cfg.hands + cfg.block - 1) / cfg.block);
    Network best_network = population[0];
    double best_validation = -INFINITY;

    printf("Training %s: %d generations, population %d, %d hands each per generation, %d threads\n",
           cfg.players == 2 ? "heads-up" : (to_string(cfg.players) + "-player").c_str(),
           cfg.generations, cfg.population, cfg.hands, cfg.threads);

    for (int gen = 1; gen <= cfg.generations; gen++) {
        auto start = chrono::steady_clock::now();

        // Opponents and deals for this generation: the same for every
        // candidate, so differences in fitness come from how they play.
        vector<vector<OpponentSpec>> lineups(blocks);
        vector<uint64_t> deck_seeds(blocks), hero_seeds(blocks);
        for (int b = 0; b < blocks; b++) {
            for (int i = 1; i < cfg.players; i++) {
                // baseline bots: 60% EquityBot, 20% CallBot, 20% RandomBot;
                // networks: half hall of fame, half current population
                OpponentSpec s;
                s.seed = rng();
                s.index = 0;
                if ((int)rng.below(100) >= cfg.self_play) {
                    int roll = rng.below(10);
                    s.kind = roll < 6 ? OPP_EQUITY : roll < 8 ? OPP_CALL : OPP_RANDOM;
                } else if (!hall.empty() && rng.below(2)) {
                    s.kind = OPP_HALL_OF_FAME;
                    s.index = rng.below(hall.size());
                } else {
                    s.kind = OPP_POPULATION;
                    s.index = rng.below(population.size());
                }
                lineups[b].push_back(s);
            }
            deck_seeds[b] = rng();
            hero_seeds[b] = rng();
        }

        vector<double> fitness(cfg.population);
        parallel_for(cfg.population, cfg.threads, [&](int c) {
            Tally t;
            for (int b = 0; b < blocks; b++) {
                vector<unique_ptr<Agent>> owned;
                vector<Agent*> opps;
                for (const OpponentSpec& s : lineups[b]) {
                    owned.push_back(make_opponent(s, hall, population, cfg.equity_samples));
                    opps.push_back(owned.back().get());
                }
                NeuralBot hero(&population[c], hero_seeds[b], cfg.equity_samples);
                int hands = min(cfg.block, cfg.hands - b * cfg.block);
                t.add(play_hands(hero, opps, hands, cfg.stack, deck_seeds[b]));
            }
            fitness[c] = t.score().chips_per_hand;
        });

        vector<int> order(cfg.population);
        for (int i = 0; i < cfg.population; i++) order[i] = i;
        sort(order.begin(), order.end(), [&](int a, int b) { return fitness[a] > fitness[b]; });
        double mean = 0;
        for (double f : fitness) mean += f;
        mean /= cfg.population;
        const Network& champion = population[order[0]];

        hall.push_back(champion);
        if ((int)hall.size() > HALL_SIZE) hall.erase(hall.begin());

        double secs = chrono::duration<double>(chrono::steady_clock::now() - start).count();
        printf("gen %3d | best %+7.2f chips/hand | mean %+7.2f | %.1fs\n", gen, fitness[order[0]], mean, secs);

        // Check the champion against EquityBots on fresh hands: unlike
        // fitness, this is comparable across generations. (Use `eval` for
        // the held-out TightAggressiveBot test.)
        if (gen % cfg.eval_every == 0 || gen == cfg.generations) {
            Score s = evaluate(champion, VS_EQUITY, cfg.players, cfg.eval_hands, cfg.stack,
                               cfg.seed + gen, cfg.threads, cfg.equity_samples);
            bool improved = s.chips_per_hand > best_validation;
            printf("        vs EquityBots: %+.2f +- %.2f chips/hand (%+.0f bb/100) over %ld hands%s\n",
                   s.chips_per_hand, s.ci95, s.bb_per_100(), s.hands, improved ? "  [new best]" : "");
            fflush(stdout);
            if (improved) {
                best_validation = s.chips_per_hand;
                best_network = champion;
                if (!cfg.out_path.empty() && !best_network.save(cfg.out_path)) {
                    printf("        couldn't save to %s\n", cfg.out_path.c_str());
                }
            }
        }

        // Next generation: keep the elites, fill the rest with mutated
        // children of parents picked by tournament (best of 3 at random).
        vector<Network> next;
        for (int i = 0; i < cfg.elites && i < cfg.population; i++) next.push_back(population[order[i]]);
        while ((int)next.size() < cfg.population) {
            int parent = rng.below(cfg.population);
            for (int k = 0; k < 2; k++) {
                int other = rng.below(cfg.population);
                if (fitness[other] > fitness[parent]) parent = other;
            }
            next.push_back(mutate(population[parent], rng, cfg));
        }
        population = std::move(next);
    }
    return best_network;
}

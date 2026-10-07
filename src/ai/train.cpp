#include "ai/train.h"
#include "bots/bot.h"
#include "engine/game.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>

string baseline_name(Baseline b) {
    switch (b) {
        case VS_RANDOM: return "RandomBot";
        case VS_CALL: return "CallBot";
        case VS_EQUITY: return "EquityBot";
        case VS_STYLES: return "StyleBot";
        case VS_TIGHT_STYLES: return "tight StyleBot";
        case VS_LOOSE_STYLES: return "loose StyleBot";
        case VS_TIGHT_AGGRESSIVE: return "TightAggressiveBot";
        case VS_LOOSE_AGGRESSIVE: return "LooseAggressiveBot";
    }
    return "?";
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
enum OpponentKind {
    OPP_RANDOM, OPP_CALL, OPP_EQUITY, OPP_STYLE, OPP_TIGHT_STYLE, OPP_LOOSE_STYLE,
    OPP_TIGHT_AGGRESSIVE, OPP_LOOSE_AGGRESSIVE,
    OPP_NETWORK // a network: a pool version, or a model to evaluate against
};

struct OpponentSpec {
    OpponentKind kind;
    int index;      // which network, for OPP_NETWORK
    uint64_t seed;  // also picks a StyleBot's style
};

// One training opponent, drawn with the mix weights. Past versions are only
// drawn when allowed and the pool isn't empty.
static OpponentSpec sample_opponent(FastRng& rng, const TrainConfig& cfg, int pool_size, bool allow_versions) {
    int versions = allow_versions && pool_size > 0 ? cfg.mix_versions : 0;
    int weights[] = {versions, cfg.mix_tight, cfg.mix_loose, cfg.mix_random, cfg.mix_equity, cfg.mix_call};
    OpponentKind kinds[] = {OPP_NETWORK, OPP_TIGHT_STYLE, OPP_LOOSE_STYLE, OPP_RANDOM, OPP_EQUITY, OPP_CALL};
    int total = 0;
    for (int w : weights) total += w;
    int roll = total > 0 ? rng.below(total) : 0;
    OpponentSpec s = {OPP_RANDOM, 0, 0};
    for (int i = 0; i < 6; i++) {
        if (roll < weights[i]) { s.kind = kinds[i]; break; }
        roll -= weights[i];
    }
    if (s.kind == OPP_NETWORK) s.index = rng.below(pool_size);
    s.seed = rng();
    return s;
}

static unique_ptr<Agent> make_opponent(const OpponentSpec& s, const vector<Network>& networks, int equity_samples) {
    FastRng style_rng(s.seed);
    switch (s.kind) {
        case OPP_RANDOM: return make_unique<RandomBot>(s.seed);
        case OPP_CALL: return make_unique<CallBot>();
        case OPP_EQUITY: return make_unique<EquityBot>(s.seed, equity_samples);
        case OPP_STYLE: return make_unique<StyleBot>(Style::random(style_rng), s.seed, equity_samples);
        case OPP_TIGHT_STYLE: return make_unique<StyleBot>(Style::tight(style_rng), s.seed, equity_samples);
        case OPP_LOOSE_STYLE: return make_unique<StyleBot>(Style::loose_aggressive(style_rng), s.seed, equity_samples);
        case OPP_TIGHT_AGGRESSIVE: return make_unique<TightAggressiveBot>(s.seed);
        case OPP_LOOSE_AGGRESSIVE: return make_unique<LooseAggressiveBot>(s.seed);
        case OPP_NETWORK: return make_unique<NeuralBot>(&networks[s.index], s.seed, equity_samples);
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

// Plays `net` in seat 0 for `hands` cash hands, in chunks of 500 with a new
// table each chunk; pick(rng) chooses each opponent.
template <class Pick>
static Tally evaluate_with(const Network& net, Pick pick, const vector<Network>& networks, int players,
                           int hands, int stack, uint64_t seed, int threads, int equity_samples) {
    const int chunk = 500;
    int chunks = (hands + chunk - 1) / chunk;
    vector<Tally> tallies(chunks);
    parallel_for(chunks, threads, [&](int c) {
        uint64_t s = seed * 1000003 + c * 7919;
        FastRng rng(s);
        NeuralBot hero(&net, s, equity_samples);
        vector<unique_ptr<Agent>> owned;
        vector<Agent*> opps;
        for (int i = 1; i < players; i++) {
            owned.push_back(make_opponent(pick(rng), networks, equity_samples));
            opps.push_back(owned.back().get());
        }
        tallies[c] = play_hands(hero, opps, min(chunk, hands - c * chunk), stack, s);
    });
    Tally total;
    for (const Tally& t : tallies) total.add(t);
    return total;
}

Score evaluate(const Network& net, Baseline opponents, int players, int hands, int stack,
               uint64_t seed, int threads, int equity_samples) {
    OpponentKind kinds[] = {OPP_RANDOM, OPP_CALL, OPP_EQUITY, OPP_STYLE, OPP_TIGHT_STYLE, OPP_LOOSE_STYLE,
                            OPP_TIGHT_AGGRESSIVE, OPP_LOOSE_AGGRESSIVE};
    OpponentKind kind = kinds[opponents];
    auto pick = [&](FastRng& rng) { return OpponentSpec{kind, 0, rng()}; };
    return evaluate_with(net, pick, {}, players, hands, stack, seed, threads, equity_samples).score();
}

Score evaluate_vs(const Network& net, const Network& opponent, int players, int hands, int stack,
                  uint64_t seed, int threads, int equity_samples) {
    vector<Network> networks = {opponent};
    auto pick = [&](FastRng& rng) { return OpponentSpec{OPP_NETWORK, 0, rng()}; };
    return evaluate_with(net, pick, networks, players, hands, stack, seed, threads, equity_samples).score();
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

    // Past versions the network plays against, and where they're saved.
    vector<Network> pool;
    vector<int> pool_gen; // generation each version came from
    filesystem::path pool_dir;
    if (!cfg.out_path.empty()) {
        filesystem::path out(cfg.out_path);
        pool_dir = out.parent_path() / (out.stem().string() + "_versions");
        filesystem::remove_all(pool_dir);
        filesystem::create_directories(pool_dir);
    }
    auto version_path = [&](int gen) {
        char name[32];
        snprintf(name, sizeof(name), "v%04d.net", gen);
        return pool_dir / name;
    };

    int blocks = max(1, (cfg.hands + cfg.block - 1) / cfg.block);
    Network newest = population[0];

    printf("Training %s: %d generations, population %d, %d hands each per generation, %d threads\n",
           cfg.players == 2 ? "heads-up" : (to_string(cfg.players) + "-player").c_str(),
           cfg.generations, cfg.population, cfg.hands, cfg.threads);
    printf("Opponent mix: %d past versions, %d tight, %d loose-aggressive, %d random, %d equity, %d call\n",
           cfg.mix_versions, cfg.mix_tight, cfg.mix_loose, cfg.mix_random, cfg.mix_equity, cfg.mix_call);

    for (int gen = 1; gen <= cfg.generations; gen++) {
        auto start = chrono::steady_clock::now();

        // Opponents and deals for this generation: the same for every
        // candidate, so differences in fitness come from how they play.
        vector<vector<OpponentSpec>> lineups(blocks);
        vector<uint64_t> deck_seeds(blocks), hero_seeds(blocks);
        for (int b = 0; b < blocks; b++) {
            for (int i = 1; i < cfg.players; i++) lineups[b].push_back(sample_opponent(rng, cfg, pool.size(), true));
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
                    owned.push_back(make_opponent(s, pool, cfg.equity_samples));
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

        double secs = chrono::duration<double>(chrono::steady_clock::now() - start).count();
        printf("gen %3d | best %+7.2f chips/hand | mean %+7.2f | pool %zu | %.1fs\n",
               gen, fitness[order[0]], mean, pool.size(), secs);

        if (gen % cfg.eval_every == 0 || gen == cfg.generations) {
            // Does the champion beat the past versions? Play a table of
            // each one (fresh deals) and combine the results.
            bool admit = pool.empty();
            if (!pool.empty()) {
                Tally against_pool;
                for (int i = 0; i < (int)pool.size(); i++) {
                    vector<Network> one = {pool[i]};
                    auto pick = [&](FastRng& r) { return OpponentSpec{OPP_NETWORK, 0, r()}; };
                    Tally t = evaluate_with(champion, pick, one, cfg.players, cfg.pool_hands, cfg.stack,
                                            cfg.seed * 7 + gen * 131 + i, cfg.threads, cfg.equity_samples);
                    against_pool.add(t);
                }
                Score p = against_pool.score();
                admit = p.chips_per_hand - p.ci95 > 0;
                printf("        vs %zu past versions: %+.2f +- %.2f chips/hand -> %s\n", pool.size(),
                       p.chips_per_hand, p.ci95, admit ? "better, joins the pool" : "not clearly better");
            }

            // Also track how it does against the fixed bots in the mix.
            auto pick = [&](FastRng& r) { return sample_opponent(r, cfg, 0, false); };
            Score v = evaluate_with(champion, pick, {}, cfg.players, cfg.eval_hands, cfg.stack,
                                    cfg.seed + gen, cfg.threads, cfg.equity_samples).score();
            printf("        vs fixed bots in the mix: %+.2f +- %.2f chips/hand (%+.0f bb/100)\n",
                   v.chips_per_hand, v.ci95, v.bb_per_100());

            if (admit) {
                if ((int)pool.size() >= cfg.pool_size) { // replace the oldest version
                    printf("        replaces v%04d, the oldest version\n", pool_gen[0]);
                    if (!pool_dir.empty()) filesystem::remove(version_path(pool_gen[0]));
                    pool.erase(pool.begin());
                    pool_gen.erase(pool_gen.begin());
                }
                pool.push_back(champion);
                pool_gen.push_back(gen);
                newest = champion;
                if (!cfg.out_path.empty()) {
                    if (!newest.save(cfg.out_path) || !newest.save(version_path(gen).string())) {
                        printf("        couldn't save to %s\n", cfg.out_path.c_str());
                    }
                }
            }
            fflush(stdout);
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
    printf("Pool: %zu versions (from generations", pool.size());
    for (int g : pool_gen) printf(" %d", g);
    printf(")\n");
    return newest;
}

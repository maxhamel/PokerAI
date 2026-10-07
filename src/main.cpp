#include "engine/game.h"
#include "bots/bot.h"
#include "ai/features.h"
#include "ai/train.h"
#include <filesystem>
#include "ui/ui.h"
#include <chrono>
#include <thread>

// Usage:
//   ./poker              you (Player 1) against 8 bots, with the table window
//   ./poker play <model> you against a trained network, at the table size it
//                        was trained for (100 big blind stacks)
//   ./poker hotseat      everyone takes turns on the same screen
//   ./poker features [seed]
//                        plays one hand with bots and prints Player 1's
//                        feature vector at each of their decisions
//   ./poker sim [games] [seed] [threads]
//                        bots only, no window; plays games on all CPU cores
//                        and reports results (same seed = same games)
//   ./poker train <6max|headsup> [option=value ...]
//                        evolves a network that maximizes chips won per hand.
//                        Options: gens pop hands block elites rate size
//                        samples temp self eval_every eval_hands seed
//                        threads out
//   ./poker eval <model> [hands]
//                        plays a trained network against tables of each
//                        baseline bot and reports chips won per hand
int main(int argc, char** argv) {
    string mode = argc > 1 ? argv[1] : "play";

    // features takes its seed as the 2nd argument, sim as the 3rd
    int seed_arg = mode == "features" ? 2 : 3;
    unsigned seed = argc > seed_arg ? atoi(argv[seed_arg]) : random_device{}();
    vector<RandomBot> bots;
    for (int i = 0; i < PLAYER_COUNT; i++) bots.emplace_back(seed + 1000 * (i + 1));

    if (mode == "features") {
        // Plays like a RandomBot but prints what a neural network would see.
        struct FeaturePrinter : Agent {
            FeatureExtractor extractor;
            RandomBot bot;
            FeaturePrinter(unsigned seed) : extractor(seed, 2000), bot(seed) {}
            Action act(const PlayerView& v) override {
                Features f;
                extractor.extract(v, f);
                cout << "\nPlayer " << v.id + 1 << " | hand: " << cards_str(v.hole)
                     << " | board: " << (v.community.empty() ? "-" : cards_str(v.community)) << '\n';
                for (int i = 0; i < F_OPPONENTS; i++) {
                    printf("  %-24s %.3f\n", FeatureExtractor::feature_name(i).c_str(), f[i]);
                }
                cout << "  opponents (clockwise):    in hand  all-in  stack  street bet\n";
                for (int k = 0; k < MAX_OPPONENTS; k++) {
                    const float* o = &f[F_OPPONENTS + k * OPPONENT_FEATURES];
                    printf("    %d %26.0f %7.0f %7.3f %8.3f\n", k + 1, o[0], o[1], o[2], o[3]);
                }
                return bot.act(v);
            }
        } printer(seed);
        vector<Agent*> agents = {&printer};
        for (int i = 1; i < PLAYER_COUNT; i++) agents.push_back(&bots[i]);
        Game game(agents, nullptr, true, seed);
        game.play_hand();
        return 0;
    }

    if (mode == "sim") {
        int games = argc > 2 ? max(1, atoi(argv[2])) : 1000;
        int threads = argc > 4 ? max(1, atoi(argv[4])) : max(1u, thread::hardware_concurrency());

        // Games are independent, so each thread plays every `threads`-th game.
        // Each game seeds its own deck and bots from its number, so results
        // don't depend on how many threads ran.
        vector<int> winner(games), hand_count(games);
        auto run_games = [&](int t) {
            for (int g = t; g < games; g += threads) {
                vector<RandomBot> game_bots;
                vector<Agent*> agents;
                for (int i = 0; i < PLAYER_COUNT; i++) game_bots.emplace_back((seed + g) * 31ULL + i + 1);
                for (RandomBot& b : game_bots) agents.push_back(&b);
                Game game(agents, nullptr, false, seed + g);
                winner[g] = game.play();
                hand_count[g] = game.hands_played();
            }
        };

        auto start = chrono::steady_clock::now();
        vector<thread> pool;
        for (int t = 0; t < threads; t++) pool.emplace_back(run_games, t);
        for (thread& t : pool) t.join();
        double secs = chrono::duration<double>(chrono::steady_clock::now() - start).count();

        vector<int> wins(PLAYER_COUNT, 0);
        long hands = 0;
        unsigned long checksum = 0; // changes if any game plays out differently
        for (int g = 0; g < games; g++) {
            wins[winner[g]]++;
            hands += hand_count[g];
            checksum = checksum * 1000003 + winner[g] * 100000 + hand_count[g];
        }

        cout << games << " games, " << hands << " hands in " << secs << "s ("
             << (long)(hands / secs) << " hands/s, " << threads << " threads)\n";
        for (int i = 0; i < PLAYER_COUNT; i++) {
            cout << "Player " << i + 1 << ": " << wins[i] << " wins\n";
        }
        cout << "checksum: " << checksum << '\n';
        return 0;
    }

    if (mode == "train") {
        string table = argc > 2 ? argv[2] : "";
        if (table != "6max" && table != "headsup") {
            cout << "usage: " << argv[0] << " train <6max|headsup> [option=value ...]\n";
            return 1;
        }
        TrainConfig cfg;
        cfg.players = table == "6max" ? 6 : 2;
        cfg.out_path = "models/" + table + ".net";
        for (int i = 3; i < argc; i++) {
            string arg = argv[i];
            size_t eq = arg.find('=');
            string key = arg.substr(0, eq), value = eq == string::npos ? "" : arg.substr(eq + 1);
            if (key == "gens") cfg.generations = stoi(value);
            else if (key == "pop") cfg.population = stoi(value);
            else if (key == "hands") cfg.hands = stoi(value);
            else if (key == "block") cfg.block = stoi(value);
            else if (key == "elites") cfg.elites = stoi(value);
            else if (key == "rate") cfg.mutation_rate = stof(value);
            else if (key == "size") cfg.mutation_size = stof(value);
            else if (key == "samples") cfg.equity_samples = stoi(value);
            else if (key == "temp") cfg.temperature = stof(value);
            else if (key == "self") cfg.self_play = stoi(value);
            else if (key == "eval_every") cfg.eval_every = stoi(value);
            else if (key == "eval_hands") cfg.eval_hands = stoi(value);
            else if (key == "seed") cfg.seed = stoull(value);
            else if (key == "threads") cfg.threads = stoi(value);
            else if (key == "out") cfg.out_path = value;
            else { cout << "unknown option: " << key << '\n'; return 1; }
        }
        filesystem::path dir = filesystem::path(cfg.out_path).parent_path();
        if (!dir.empty()) filesystem::create_directories(dir);
        train(cfg);
        cout << "saved best network to " << cfg.out_path << '\n';
        return 0;
    }

    if (mode == "eval") {
        Network net;
        string error;
        if (argc < 3 || !Network::load(argv[2], net, error)) {
            cout << (argc < 3 ? "usage: " + string(argv[0]) + " eval <model> [hands]" : error) << '\n';
            return 1;
        }
        int hands = argc > 3 ? atoi(argv[3]) : 100000;
        int threads = max(1u, thread::hardware_concurrency());
        printf("%s (%d players), %d hands per table (TightAggressiveBot is never seen in training):\n",
               argv[2], net.players, hands);
        for (Baseline b : {VS_RANDOM, VS_CALL, VS_EQUITY, VS_TIGHT_AGGRESSIVE}) {
            Score s = evaluate(net, b, net.players, hands, 200, 12345, threads);
            printf("  vs %-19s %+7.2f +- %.2f chips/hand  (%+.0f bb/100)\n",
                   (baseline_name(b) + "s").c_str(), s.chips_per_hand, s.ci95, s.bb_per_100());
        }
        return 0;
    }

    if (mode != "play" && mode != "hotseat") {
        cout << "usage: " << argv[0] << " [play [model] | hotseat | features [seed] | sim [games] [seed] [threads]\n"
             << "       | train <6max|headsup> [option=value ...] | eval <model> [hands]]\n";
        return 1;
    }

    // play against a trained network, if one was given
    Network net;
    bool use_net = mode == "play" && argc > 2;
    if (use_net) {
        string error;
        if (!Network::load(argv[2], net, error)) {
            cout << error << '\n';
            return 1;
        }
    }
    int players = use_net ? net.players : PLAYER_COUNT;
    vector<unique_ptr<NeuralBot>> net_bots;

    TableUI ui;
    vector<Agent*> agents;
    for (int i = 0; i < players; i++) {
        bool human = mode == "hotseat" || i == 0;
        if (human) agents.push_back(&ui);
        else if (use_net) {
            net_bots.push_back(make_unique<NeuralBot>(&net, seed + i, 100));
            agents.push_back(net_bots.back().get());
        } else agents.push_back(&bots[i]);
    }
    Game game(agents, &ui, true);
    if (use_net) game.set_stacks(200); // the 100 big blinds it was trained with
    game.play();
}

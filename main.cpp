#include "game.h"
#include "bot.h"
#include "ui.h"
#include <chrono>
#include <thread>

// Usage:
//   ./poker              you (Player 1) against 8 bots, with the table window
//   ./poker hotseat      everyone takes turns on the same screen
//   ./poker sim [games] [seed] [threads]
//                        bots only, no window; plays games on all CPU cores
//                        and reports results (same seed = same games)
int main(int argc, char** argv) {
    string mode = argc > 1 ? argv[1] : "play";

    unsigned seed = argc > 3 ? atoi(argv[3]) : random_device{}();
    vector<RandomBot> bots;
    for (int i = 0; i < PLAYER_COUNT; i++) bots.emplace_back(seed + 1000 * (i + 1));

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

    if (mode != "play" && mode != "hotseat") {
        cout << "usage: " << argv[0] << " [play | hotseat | sim [games]]\n";
        return 1;
    }

    TableUI ui;
    vector<Agent*> agents;
    for (int i = 0; i < PLAYER_COUNT; i++) {
        bool human = mode == "hotseat" || i == 0;
        agents.push_back(human ? (Agent*)&ui : &bots[i]);
    }
    Game game(agents, &ui, true);
    game.play();
}

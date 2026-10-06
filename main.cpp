#include "game.h"
#include "bot.h"
#include "ui.h"
#include <chrono>

// Usage:
//   ./poker              you (Player 1) against 8 bots, with the table window
//   ./poker hotseat      everyone takes turns on the same screen
//   ./poker sim [games]  bots only, no window; plays games and reports results
int main(int argc, char** argv) {
    string mode = argc > 1 ? argv[1] : "play";

    vector<RandomBot> bots;
    for (int i = 0; i < PLAYER_COUNT; i++) bots.emplace_back(random_device{}());

    if (mode == "sim") {
        int games = argc > 2 ? max(1, atoi(argv[2])) : 1000;
        vector<Agent*> agents;
        for (RandomBot& b : bots) agents.push_back(&b);

        vector<int> wins(PLAYER_COUNT, 0);
        long hands = 0;
        auto start = chrono::steady_clock::now();
        for (int g = 0; g < games; g++) {
            Game game(agents);
            wins[game.play()]++;
            hands += game.hands_played();
        }
        double secs = chrono::duration<double>(chrono::steady_clock::now() - start).count();

        cout << games << " games, " << hands << " hands in " << secs << "s ("
             << (long)(hands / secs) << " hands/s)\n";
        for (int i = 0; i < PLAYER_COUNT; i++) {
            cout << "Player " << i + 1 << ": " << wins[i] << " wins\n";
        }
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

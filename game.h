#pragma once

#include "types.h"
#include "agent.h"
#include "rng.h"

class Player {
    private:

        int chip_count = STARTING_CHIP_COUNT;
        int street_bet = 0;
        int total_bet = 0;
        int id;
        int position;
        vector <Card> cards;
        bool folded = true;   

    public:

        Player(int id) : id(id), position(id){
            cards.reserve(2);
        }

        const vector<Card>& get_hand() const {
            return cards;
        }

        int get_id() const { return id; }

        int get_chips() const { return chip_count; }

        void deal_card(Card c) {
            cards.push_back(c);
        }

        int get_total_bet() const { return total_bet; }

        bool is_folded() const { return folded; }

        bool is_all_in() const { return !folded && chip_count == 0; }

        int get_street_bet() const { return street_bet; }

        // Moves chips from the stack into the pot, capped at the stack (all-in).
        int bet(int amount) {
            amount = min(amount, chip_count);
            chip_count -= amount;
            street_bet += amount;
            total_bet += amount;
            return amount;
        }

        void fold() {
            folded = true;
        }

        void new_street() {
            street_bet = 0;
        }

        void new_hand() {
            cards.clear();
            folded = chip_count == 0; // busted players sit out
            street_bet = 0;
            total_bet = 0;
        }

        void add_chips(int x) {
            chip_count += x;
        }
};

// Runs the game: dealing, betting, showdowns and payouts. It knows nothing
// about how actions are chosen; each seat's Agent decides that.
class Game {
    private:

        vector <Player> players;
        vector <Agent*> agents;   // by seat
        GameObserver* observer;   // optional, e.g. the UI
        bool verbose;             // print the action log to the terminal
        bool logging;             // build log/action text; off when nobody reads it (training)
        FastRng rng;              // shuffles the deck; seed it for repeatable games
        vector <Card> deck;
        int cards_left = 0;       // deck[0, cards_left) haven't been dealt this hand
        vector <Card> community;
        Street street = PREFLOP;
        int button_pos = 0;
        int pot_size = 0;
        int current_bet = 0; // highest street bet this round
        int min_raise = BIG_BLIND;
        int hand_counter = 0;
        int sb_pos = 0;
        int bb_pos = 0;
        vector<string> last_action; // by seat, this street
        vector<string> log_lines;
        PlayerView view;            // reused for every decision
        // scratch space reused every hand, so the game loop doesn't allocate
        vector<int> acted_at_buf, bets_buf, scores_buf, won_buf, winners_buf;

        void createDeck();
        void createPlayers();
        void log(const string& line);
        void reset_hand();
        int live_count();
        int next_seat_with_chips(int seat);
        bool can_act(const Player& p);
        void post_blinds(int sb, int bb);
        void new_street();
        PlayerView table_view();
        void fill_public(PlayerView& v);
        const PlayerView& make_view(const Player& p, bool can_raise);
        Action get_action(const PlayerView& v);
        void betting_round(int first);
        Card draw();
        void deal_preflop();
        void deal(int amount);
        const vector<int>& payouts();

    public:

        Game(const vector<Agent*>& agents, GameObserver* observer = nullptr, bool verbose = false,
             unsigned seed = random_device{}());

        int play();
        void play_hand();
        int players_with_chips();
        int hands_played() const { return hand_counter; }
        const vector<Player>& get_players() const { return players; }
};

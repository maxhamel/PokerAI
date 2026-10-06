#include "types.h"
#include "ui.h"

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

        vector<Card> get_hand() const {
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

class Game {
    private:

        vector <Player> players;
        vector <Card> deck;
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
        TableUI ui;
        
        void createDeck() {

            deck.reserve(52);
            community.reserve(5);

            for (int s = 0; s < NUMBER_OF_SUITS; s++) {
                for(int r = 0; r < NUMBER_OF_RANKS; r++) {
                    Card curr_card{static_cast<Suit>(s), static_cast<Rank>(r)};
                    deck.push_back(curr_card);
                }
            }
        }

        void createPlayers() {
            players.reserve(PLAYER_COUNT);

            for (int i = 0; i < PLAYER_COUNT; i++) {
                Player curr_player(i);
                players.push_back(curr_player);
            }
        }

    public:

        Game() {
            createDeck();
            createPlayers();
            last_action.assign(players.size(), "");
        }

        void log(const string& line) {
            cout << line << '\n';
            log_lines.push_back(line);
        }

        void reset_hand() {
            deck.clear();
            community.clear();
            for (Player &p: players) p.new_hand();
            last_action.assign(players.size(), "");
            createDeck();
            shuffle();
            deal_preflop();
        }

        int live_count() {
            int count = 0;
            for (Player &p: players) if (!p.is_folded()) count++;
            return count;
        }

        int players_with_chips() {
            int count = 0;
            for (Player &p: players) if (p.get_chips() > 0) count++;
            return count;
        }

        // Next seat clockwise from `seat` that has chips, skipping busted players.
        int next_seat_with_chips(int seat) {
            int n = players.size();
            for (int i = 1; i <= n; i++) {
                int s = (seat + i) % n;
                if (players[s].get_chips() > 0) return s;
            }
            return seat;
        }

        bool can_act(const Player& p) {
            return !p.is_folded() && !p.is_all_in();
        }

        void post_blinds(int sb, int bb) {
            pot_size += players[sb].bet(SMALL_BLIND);
            pot_size += players[bb].bet(BIG_BLIND);
            current_bet = BIG_BLIND;
            min_raise = BIG_BLIND;
        }

        void new_street() {
            for (Player &p: players) p.new_street();
            last_action.assign(players.size(), "");
            current_bet = 0;
            min_raise = BIG_BLIND;
        }

        // Public information only: what anyone watching the table can see.
        PlayerView table_view() {
            PlayerView v;
            v.id = -1;
            v.community = community;
            v.hand_number = hand_counter;
            v.button = button_pos;
            v.small_blind = sb_pos;
            v.big_blind = bb_pos;
            v.pot = pot_size;
            v.to_call = 0;
            v.current_bet = current_bet;
            v.min_raise_to = 0;
            v.max_raise_to = 0;
            v.can_raise = false;
            for (Player &q: players) {
                v.stacks.push_back(q.get_chips());
                v.street_bets.push_back(q.get_street_bet());
                v.folded.push_back(q.is_folded());
            }
            v.last_action = last_action;
            int first = max(0, (int)log_lines.size() - 7);
            v.log.assign(log_lines.begin() + first, log_lines.end());
            return v;
        }

        PlayerView make_view(const Player& p, bool can_raise) {
            PlayerView v = table_view();
            v.id = p.get_id();
            v.hole = p.get_hand();
            v.to_call = current_bet - p.get_street_bet();
            v.max_raise_to = p.get_street_bet() + p.get_chips();
            v.min_raise_to = min(current_bet + min_raise, v.max_raise_to); // short all-in allowed
            v.can_raise = can_raise && v.max_raise_to > current_bet;
            return v;
        }

        // Asks for a legal action using only what the player can see.
        // Swap this out for a bot/AI later.
        Action get_action(const PlayerView& v) {
            return ui.get_action(v);
        }

        // Runs one street of betting. Ends once everyone who can act has
        // acted since the last raise, or only one player is left.
        void betting_round(int first) {
            int n = players.size();
            vector<int> acted_at(n, -1);       // current_bet when each player last acted
            int last_full_raise = current_bet; // bet level set by the last complete raise
            int to_act = 0;
            for (Player &p: players) if (can_act(p)) to_act++;

            for (int i = first % n; to_act > 0 && live_count() > 1; i = (i + 1) % n) {
                Player &p = players[i];
                if (!can_act(p)) continue;

                int to_call = current_bet - p.get_street_bet();
                // nobody left to bet against (everyone else folded or all-in)
                int others = 0;
                for (Player &q: players) if (&q != &p && can_act(q)) others++;
                if (others == 0 && to_call == 0) break;

                // An all-in raise smaller than a full raise doesn't reopen
                // betting for players who already acted.
                bool can_raise = others > 0 && (acted_at[i] == -1 || last_full_raise > acted_at[i]);
                PlayerView v = make_view(p, can_raise);
                Action a = get_action(v);
                if (!is_legal(v, a)) a = {to_call == 0 ? CHECK : FOLD};

                string did;
                if (a.type == FOLD) {
                    p.fold();
                    did = "Fold";
                } else if (a.type == CHECK) {
                    did = "Check";
                } else if (a.type == CALL) {
                    pot_size += p.bet(to_call);
                    did = "Call " + to_string(p.get_street_bet());
                } else if (a.type == RAISE) {
                    if (a.amount - current_bet >= min_raise) {
                        min_raise = a.amount - current_bet;
                        last_full_raise = a.amount;
                    }
                    did = (current_bet == 0 ? "Bet " : "Raise to ") + to_string(a.amount);
                    current_bet = a.amount;
                    pot_size += p.bet(a.amount - p.get_street_bet());
                }
                if (p.is_all_in()) did += " (all-in)";
                last_action[i] = did;
                log("Player " + to_string(i + 1) + ": " + did);
                acted_at[i] = current_bet;

                if (a.type == RAISE) {
                    // everyone else has to respond to the raise
                    to_act = 0;
                    for (Player &q: players) if (&q != &p && can_act(q)) to_act++;
                } else {
                    to_act--;
                }
            }
        }

        void play_hand() {
            reset_hand();
            hand_counter++;
            log("--- Hand #" + to_string(hand_counter) + " ---");

            // Heads-up, the button posts the small blind and acts first preflop.
            sb_pos = live_count() == 2 ? button_pos : next_seat_with_chips(button_pos);
            bb_pos = next_seat_with_chips(sb_pos);
            post_blinds(sb_pos, bb_pos);
            betting_round(bb_pos + 1); // preflop starts left of the big blind

            int cards_per_street[] = {3, 1, 1};
            for (int cards : cards_per_street) {
                if (live_count() <= 1) break;
                deal(cards);
                new_street();
                log(community.size() == 3 ? "Flop" : community.size() == 4 ? "Turn" : "River");
                betting_round(button_pos + 1); // postflop starts left of the button
            }

            HandResult result;
            result.shown.assign(players.size(), {});
            result.hand_name.assign(players.size(), "");
            if (live_count() > 1) { // a hand won by folds isn't shown
                for (Player &p: players) {
                    if (p.is_folded()) continue;
                    vector<Card> hand = p.get_hand();
                    hand.insert(hand.end(), community.begin(), community.end());
                    result.shown[p.get_id()] = p.get_hand();
                    result.hand_name[p.get_id()] = HAND_NAMES[score_category(evaluate(hand))];
                }
            }
            result.won = payouts();
            ui.show_result(table_view(), result);
            button_pos = next_seat_with_chips(button_pos);
        }

        // Plays hands until one player has all the chips.
        void play() {
            while (players_with_chips() > 1) play_hand();
            for (Player &p: players) {
                if (p.get_chips() > 0) ui.show_game_over(table_view(), p.get_id());
            }
        }

        void shuffle() {
            random_device rd;
            mt19937 rng(rd());

            std::shuffle(deck.begin(), deck.end(), rng);
        }

        void deal_preflop() {
            for (Player &p: players) {
                if (p.is_folded()) continue; // busted players aren't dealt in
                p.deal_card(deck.back());
                deck.pop_back();
                p.deal_card(deck.back());
                deck.pop_back();
            }
        }

        void deal(int amount) {
            for (int i = 0; i < amount; i++) {
                community.push_back(deck.back());
                deck.pop_back();
            }
        }

        int find_straight(const vector<Card>& cards) {
            vector<int> ranks;
            for (const Card& c : cards) {
                ranks.push_back(c.rank);
            }
            sort(ranks.begin(), ranks.end(), greater<int>());
            ranks.erase(unique(ranks.begin(), ranks.end()), ranks.end());

            if (!ranks.empty() && ranks[0] == ACE) {
                ranks.push_back(TWO - 1);
            }

            for (size_t i = 0; i + 4 < ranks.size(); i++) {
                if (ranks[i] - ranks[i + 4] == 4) {
                    return ranks[i];
                }
            }
            return -1;
        }

        int make_score(Hand_rankings category, const vector<int>& ranks) {
            int score = category;
            for (size_t i = 0; i < 5; i++) {
                score = score * 16 + (i < ranks.size() ? ranks[i] : 0);
            }
            return score;
        }

        Hand_rankings score_category(int score) {
            return static_cast<Hand_rankings>(score >> 20);
        }

        vector<int> top_ranks(const array<int, 13>& count, const vector<int>& exclude, int n) {
            vector<int> out;
            for (int r = ACE; r >= TWO && (int)out.size() < n; r--) {
                if (count[r] > 0 && find(exclude.begin(), exclude.end(), r) == exclude.end()) {
                    out.push_back(r);
                }
            }
            return out;
        }

        int evaluate(const vector<Card>& hand) {
            array<int, 13> rank_count{};
            array<int, 4> suit_count{};
            for (const Card& c : hand) {
                rank_count[c.rank]++;
                suit_count[c.suit]++;
            }

            // Straight flush and flush
            auto flush_it = max_element(suit_count.begin(), suit_count.end());
            bool is_flush = *flush_it >= 5;
            vector<Card> flush_cards;
            if (is_flush) {
                Suit flush_suit = static_cast<Suit>(flush_it - suit_count.begin());
                for (const Card& c : hand) {
                    if (c.suit == flush_suit) flush_cards.push_back(c);
                }
                int sf_high = find_straight(flush_cards);
                if (sf_high != -1) return make_score(STRAIGHT_FLUSH, {sf_high});
            }

            // Group ranks by how many times they appear, highest rank first
            vector<int> quads, trips, pairs;
            for (int r = ACE; r >= TWO; r--) {
                if (rank_count[r] == 4) quads.push_back(r);
                else if (rank_count[r] == 3) trips.push_back(r);
                else if (rank_count[r] == 2) pairs.push_back(r);
            }

            //Quads
            if (!quads.empty()) {
                vector<int> k = top_ranks(rank_count, {quads[0]}, 1);
                return make_score(QUADS, {quads[0], k[0]});
            }

            //Full House
            if (!trips.empty()) {
                int pair_rank = -1;
                if (trips.size() >= 2) pair_rank = trips[1];
                if (!pairs.empty()) pair_rank = max(pair_rank, pairs[0]);
                if (pair_rank != -1) return make_score(FULL_HOUSE, {trips[0], pair_rank});
            }

            //Flush
            if (is_flush) {
                vector<int> ranks;
                for (const Card& c : flush_cards) ranks.push_back(c.rank);
                sort(ranks.begin(), ranks.end(), greater<int>());
                ranks.resize(5);  
                return make_score(FLUSH, ranks);
            }

            //Straight
            int straight_high = find_straight(hand);
            if (straight_high != -1) return make_score(STRAIGHT, {straight_high});

            //Trips
            if (!trips.empty()) {
                vector<int> k = top_ranks(rank_count, {trips[0]}, 2);
                return make_score(TRIPS, {trips[0], k[0], k[1]});
            }

            //Two pair
            if (pairs.size() >= 2) {
                vector<int> k = top_ranks(rank_count, {pairs[0], pairs[1]}, 1);
                return make_score(TWO_PAIR, {pairs[0], pairs[1], k[0]});
            }

            //Pair
            if (pairs.size() == 1) {
                vector<int> k = top_ranks(rank_count, {pairs[0]}, 3);
                return make_score(PAIR, {pairs[0], k[0], k[1], k[2]});
            }

            //High card
            return make_score(HIGH_CARD, top_ranks(rank_count, {}, 5));
        }

        // Splits the pot into main/side pots by repeatedly peeling off the
        // smallest remaining bet among live players.
        vector<int> payouts() {
            int n = players.size();
            bool showdown = live_count() > 1; // no need to evaluate a hand won by folds
            vector<int> bets(n);
            vector<int> scores(n, -1);
            vector<int> won(n, 0);
            for (Player& p : players) {
                bets[p.get_id()] = p.get_total_bet();
                if (!p.is_folded() && showdown) {
                    vector<Card> hand = p.get_hand();
                    hand.insert(hand.end(), community.begin(), community.end());
                    scores[p.get_id()] = evaluate(hand);
                }
            }

            while (true) {
                // smallest remaining bet among live players
                int layer = 0;
                for (Player& p : players) {
                    int b = bets[p.get_id()];
                    if (!p.is_folded() && b > 0 && (layer == 0 || b < layer)) layer = b;
                }
                if (layer == 0) break;

                // best hand among live players still in this layer
                int best = -1;
                vector<int> winners;
                for (Player& p : players) {
                    int id = p.get_id();
                    if (p.is_folded() || bets[id] == 0) continue;
                    if (scores[id] > best) { best = scores[id]; winners = {id}; }
                    else if (scores[id] == best) winners.push_back(id);
                }

                // take up to `layer` from everyone (folded players included)
                int pot = 0;
                for (int& b : bets) {
                    int take = min(b, layer);
                    pot += take;
                    b -= take;
                }

                // odd chips go to the first winners clockwise from the button
                sort(winners.begin(), winners.end(), [&](int a, int b) {
                    return (a - button_pos - 1 + n) % n < (b - button_pos - 1 + n) % n;
                });
                int share = pot / winners.size();
                int odd = pot % winners.size();
                for (int k = 0; k < (int)winners.size(); k++) {
                    won[winners[k]] += share + (k < odd ? 1 : 0);
                }
            }

            for (int id = 0; id < n; id++) {
                if (won[id] == 0) continue;
                players[id].add_chips(won[id]);
                log("Player " + to_string(id + 1) + " wins " + to_string(won[id]));
            }
            pot_size = 0;
            return won;
        }
};

int main() {
    Game poker_game;
    poker_game.play();
}
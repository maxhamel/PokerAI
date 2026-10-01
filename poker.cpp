#include <iostream>
#include <array>
#include <vector>
#include <algorithm>
#include <random>

using namespace std;

const int STARTING_CHIP_COUNT = 100;
const int PLAYER_COUNT = 9;
const int NUMBER_OF_RANKS = 13;
const int NUMBER_OF_SUITS = 4;

const char* SUITS[] = {"HEART", "DIAMOND", "SPADE", "CLUB"};
const char* RANKS[] = {"ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", 
                    "EIGHT", "NINE", "TEN", "JACK", "QUEEN", "KING", "ACE"};
const char* HAND_NAMES[] = {"NONE", "HIGH CARD", "PAIR", "TWO PAIR", "THREE OF A KIND",
                    "STRAIGHT", "FLUSH", "FULL HOUSE", "FOUR OF A KIND", "STRAIGHT FLUSH"};

enum Suit {
    HEART, DIAMOND, SPADE, CLUB
};
enum Rank {
    TWO, THREE, FOUR, FIVE, SIX, SEVEN, 
    EIGHT, NINE, TEN, JACK, QUEEN, KING, ACE
};
enum Hand_rankings {
    NONE, HIGH_CARD, PAIR, TWO_PAIR, TRIPS, STRAIGHT, FLUSH, FULL_HOUSE, QUADS, STRAIGHT_FLUSH
};
enum Street {
    PREFLOP, FLOP, TURN, RIVER
};
enum Position {
    BB, SB, UTG, UTG1, MP, LJ, HJ, CO, BTN
};
struct Card {
    Suit suit;
    Rank rank;
    
    Card(Suit s, Rank r) : suit(s), rank(r) {}
};

class Player {
    private:

        int chip_count = 0;
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

        vector<Card> get_hand() {
            return cards;
        }

        int get_id() {
            return id;
        }

        void deal_card(Card c) {
            cards.push_back(c);
        }

        void new_hand() {
            cards.clear();
            folded = false;
            street_bet = 0;
            total_bet = 0;
        }

        void print_hand() {
            for (Card c: cards) {
                cout << RANKS[static_cast<int>(c.rank)] << " OF " 
                    << SUITS[static_cast<int>(c.suit)] << "S, ";
            }
            cout << '\n';
        }
};

class Game {
    private:

        vector <Player> players;
        vector <Player> players_in_hand;
        vector <Card> deck;
        vector <Card> community;
        Street street = PREFLOP;
        int button_pos = 0;
        int pot_size = 0;
        int street_bet = 0; 
        int hand_counter = 0;
        
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
        }

        void reset_hand() {
            deck.clear();
            community.clear();
            for (Player &p: players) p.new_hand();
            createDeck();
            shuffle();
            deal_preflop();
            players_in_hand = players;
        }

        void shuffle() {
            std::random_device rd;
            std::mt19937 rng(rd());

            std::shuffle(deck.begin(), deck.end(), rng);
        }

        void deal_preflop() {
            for (Player &p: players) {
                p.deal_card(deck.back());
                deck.pop_back();
                p.deal_card(deck.back());
                deck.pop_back();
            }
        }

        void print_community() {
            for (Card c: community) {
                cout << RANKS[static_cast<int>(c.rank)] << " OF " 
                    << SUITS[static_cast<int>(c.suit)] << "S, ";
            }
            cout << '\n';
        }

        void print_summary() {
            int i = 1;
            cout << "PLAYER HANDS: " << '\n' << "------------------" << '\n';
            for (Player &p: players) {
                cout << "Player " << i << "'s Hand: ";
                p.print_hand();
                i++;
            }
            cout << '\n';
            cout << "COMMUNITY CARDS: " << '\n' << "------------------" << '\n';
            print_community();

            cout << '\n';
            cout << "WINNERS: " << '\n' << "------------------" << '\n';
            vector<Player> winners = showdown();
            for (Player &p : winners) {
                vector<Card> hand = p.get_hand();
                hand.insert(hand.end(), community.begin(), community.end());
                int score = evaluate(hand);

                cout << "Player " << (p.get_id() + 1) << " - "
                    << HAND_NAMES[score_category(score)] << '\n';
            }
        }

        void deal(int amount) {
            for (int i = 0; i < amount; i++) {
                community.push_back(deck.back());
                deck.pop_back();
            }
        }

        int find_straight(const std::vector<Card>& cards) {
            std::vector<int> ranks;
            for (const Card& c : cards) {
                ranks.push_back(c.rank);
            }
            std::sort(ranks.begin(), ranks.end(), std::greater<int>());
            ranks.erase(std::unique(ranks.begin(), ranks.end()), ranks.end());

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

        vector<Player> showdown() {
            if (players_in_hand.size() == 1) return players_in_hand;

            int best_score = -1;
            vector<Player> winners;

            for (Player& p : players_in_hand) {
                vector<Card> hand = p.get_hand();
                hand.insert(hand.end(), community.begin(), community.end());

                int score = evaluate(hand);
                if (score > best_score) {
                    winners.clear();
                    winners.push_back(p);
                    best_score = score;
                } else if (score == best_score) {
                    winners.push_back(p);
                }
            }
            return winners;
        }
};

int main() {
    Game poker_game;

    /**
     * Main Game Loop
     * Shuffle cards
     * Deal Cards to players
     * Preflop betting
     * Deal flop
     * Post flop betting
     * Deal turn
     * turn betting
     * Deal river
     * River betting
     */
    bool Playing = true;

    // while (Playing) {
    poker_game.reset_hand();
    poker_game.deal(3);
    poker_game.deal(1);
    poker_game.deal(1);
    poker_game.print_summary();

    
    // }

}
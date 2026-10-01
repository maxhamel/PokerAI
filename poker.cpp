#include <iostream>
#include <vector>
#include <algorithm>
#include <random>

using namespace std;

const int STARTING_CHIP_COUNT = 100;
const int PLAYER_COUNT = 9;
const int NUMBER_OF_RANKS = 14;
const int NUMBER_OF_SUITS = 4;

const char* SUITS[] = {"HEART", "DIAMOND", "SPADE", "CLUB"};
const char* RANKS[] = {"ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", 
                    "EIGHT", "NINE", "TEN", "JACK", "QUEEN", "KING", "ACE"};

enum Suit {
    HEART, DIAMOND, SPADE, CLUB
};
enum Rank {
    ONE, TWO, THREE, FOUR, FIVE, SIX, SEVEN, 
    EIGHT, NINE, TEN, JACK, QUEEN, KING, ACE
};

enum Round {
    PREFLOP, FLOP, TURN, RIVER
};

struct Card {
    enum Suit suit;
    enum Rank rank;
    
    Card(Suit s, Rank r) : suit(s), rank(r) {}

    Suit get_suit() { return suit; }
    Rank get_rank() { return rank; }
};

class Player {
    private:

        int chip_count = 0;
        int position;
        vector <Card> cards;
        bool folded = true;   

    public:

        Player(int p) : position(p) {
            cards.reserve(2);
        }

        void deal_card(Card c) {
            cards.push_back(c);
        }

        void new_hand() {
            cards.clear();
            folded = false;
        }

        void print_hand() {
            for (Card c: cards) {
                cout << RANKS[static_cast<int>(c.get_rank())] << " OF " 
                    << SUITS[static_cast<int>(c.get_suit())] << "S, ";
            }
            cout << '\n';
        }
};

class Game {
    private:

        vector <Player> players;
        vector <Card> deck;
        vector <Card> community;
        enum Round round = PREFLOP;
        int players_in_hand = PLAYER_COUNT;
        int button_pos = 0;
        int pot_size = 0;
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
            players_in_hand = PLAYER_COUNT;
            deal_preflop();
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
                cout << RANKS[static_cast<int>(c.get_rank())] << " OF " 
                    << SUITS[static_cast<int>(c.get_suit())] << "S, ";
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
        }

        void deal(int amount) {
            for (int i = 0; i < amount; i++) {
                community.push_back(deck.back());
                deck.pop_back();
            }
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
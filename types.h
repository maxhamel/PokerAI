#pragma once

#include <iostream>
#include <array>
#include <vector>
#include <algorithm>
#include <random>
#include <string>

using namespace std;

const int STARTING_CHIP_COUNT = 100;
const int SMALL_BLIND = 1;
const int BIG_BLIND = 2;
const int PLAYER_COUNT = 9;
const int NUMBER_OF_RANKS = 13;
const int NUMBER_OF_SUITS = 4;

inline const char* SUITS[] = {"HEART", "DIAMOND", "SPADE", "CLUB"};
inline const char* RANKS[] = {"TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN",
                    "EIGHT", "NINE", "TEN", "JACK", "QUEEN", "KING", "ACE"};
inline const char* HAND_NAMES[] = {"NONE", "HIGH CARD", "PAIR", "TWO PAIR", "THREE OF A KIND",
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

enum ActionType {
    FOLD, CHECK, CALL, RAISE
};
struct Action {
    ActionType type;
    int amount = 0; // for RAISE: total street bet to raise to
};

// Everything one player is allowed to know when it's their turn to act.
// With id = -1 it's a spectator's view: public information only.
struct PlayerView {
    int id;
    vector<Card> hole;
    vector<Card> community;
    int hand_number;
    int button;
    int small_blind;
    int big_blind;
    int pot;
    int to_call;
    int current_bet;
    int min_raise_to;
    int max_raise_to;           // all-in
    bool can_raise;
    vector<int> stacks;         // by seat
    vector<int> street_bets;    // by seat
    vector<bool> folded;        // by seat
    vector<string> last_action; // by seat, this street
    vector<string> log;         // recent actions, oldest first
};

// What gets revealed once a hand is over.
struct HandResult {
    vector<vector<Card>> shown; // by seat; empty if the hand wasn't shown
    vector<string> hand_name;   // by seat; empty if not shown
    vector<int> won;            // by seat
};

inline string card_name(const Card& c) {
    return string(RANKS[c.rank]) + " OF " + SUITS[c.suit] + "S";
}

inline string cards_str(const vector<Card>& cards) {
    string out;
    for (const Card& c : cards) {
        if (!out.empty()) out += ", ";
        out += card_name(c);
    }
    return out;
}

inline bool is_legal(const PlayerView& v, const Action& a) {
    switch (a.type) {
        case FOLD:  return true;
        case CHECK: return v.to_call == 0;
        case CALL:  return v.to_call > 0;
        case RAISE: return v.can_raise && a.amount >= v.min_raise_to && a.amount <= v.max_raise_to;
    }
    return false;
}

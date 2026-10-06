#pragma once

#include "types.h"

// Scores a hand of up to 7 cards (best five count). Higher score = better hand.
int evaluate(const vector<Card>& hand);

// The hand category (pair, flush, ...) of a score from evaluate().
Hand_rankings score_category(int score);

// Highest card of a straight in `cards`, or -1 if there isn't one.
int find_straight(const vector<Card>& cards);

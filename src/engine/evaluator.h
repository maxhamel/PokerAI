#pragma once

#include "engine/types.h"
#include <span>

// Scores the best five-card hand made from `hole` plus `board` (5 to 7 cards
// in total). Higher score = better hand; equal scores tie. Doesn't allocate,
// so it's cheap to call many times (e.g. for simulating odds).
int evaluate(span<const Card> hole, span<const Card> board = {});

// The hand category (pair, flush, ...) of a score from evaluate().
Hand_rankings score_category(int score);

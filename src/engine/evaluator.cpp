#include "engine/evaluator.h"

// Hands are scored as category << 20 followed by up to five 4-bit ranks
// (most important first), e.g. a pair of kings with A-9-4 kickers is
// PAIR, K, A, 9, 4. Comparing scores as integers then compares hands.
//
// Ranks are tracked as 13-bit masks: bit r is set if rank r (TWO = 0 ...
// ACE = 12) is present.

static int make_score(Hand_rankings category, int r0 = 0, int r1 = 0, int r2 = 0, int r3 = 0, int r4 = 0) {
    return category << 20 | r0 << 16 | r1 << 12 | r2 << 8 | r3 << 4 | r4;
}

static int highest(unsigned mask) {
    return 31 - __builtin_clz(mask);
}

// Highest card of a straight in `mask`, or -1. An ace also counts as low (A-2-3-4-5).
static int straight_high(unsigned mask) {
    unsigned m = (mask << 1) | (mask >> ACE & 1); // bit 0 = low ace, bit r+1 = rank r
    unsigned runs = m & (m >> 1) & (m >> 2) & (m >> 3) & (m >> 4);
    return runs ? highest(runs) + 3 : -1;
}

// Fills `out` with the n highest ranks in `mask`, highest first.
static void top_ranks(unsigned mask, int n, int* out) {
    for (int i = 0; i < n; i++) {
        out[i] = mask ? highest(mask) : 0;
        if (mask) mask &= ~(1u << out[i]);
    }
}

int evaluate(span<const Card> hole, span<const Card> board) {
    int rank_count[13] = {};
    unsigned suit_mask[4] = {};
    int suit_count[4] = {};
    for (span<const Card> cards : {hole, board}) {
        for (const Card& c : cards) {
            rank_count[c.rank]++;
            suit_mask[c.suit] |= 1u << c.rank;
            suit_count[c.suit]++;
        }
    }

    // Straight flush and flush
    int flush_suit = -1;
    for (int s = 0; s < 4; s++) if (suit_count[s] >= 5) flush_suit = s;
    if (flush_suit != -1) {
        int sf = straight_high(suit_mask[flush_suit]);
        if (sf != -1) return make_score(STRAIGHT_FLUSH, sf);
    }

    // Group ranks by how many times they appear
    unsigned any = 0, quads = 0, trips = 0, pairs = 0;
    for (int r = 0; r < 13; r++) {
        unsigned bit = 1u << r;
        if (rank_count[r]) any |= bit;
        if (rank_count[r] == 4) quads |= bit;
        else if (rank_count[r] == 3) trips |= bit;
        else if (rank_count[r] == 2) pairs |= bit;
    }

    int k[5];
    if (quads) {
        int q = highest(quads);
        top_ranks(any & ~(1u << q), 1, k);
        return make_score(QUADS, q, k[0]);
    }

    if (trips) {
        int t = highest(trips);
        unsigned pair_candidates = (trips & ~(1u << t)) | pairs; // a second set of trips also works
        if (pair_candidates) return make_score(FULL_HOUSE, t, highest(pair_candidates));
    }

    if (flush_suit != -1) {
        top_ranks(suit_mask[flush_suit], 5, k);
        return make_score(FLUSH, k[0], k[1], k[2], k[3], k[4]);
    }

    int st = straight_high(any);
    if (st != -1) return make_score(STRAIGHT, st);

    if (trips) {
        int t = highest(trips);
        top_ranks(any & ~(1u << t), 2, k);
        return make_score(TRIPS, t, k[0], k[1]);
    }

    if (pairs) {
        int p1 = highest(pairs);
        unsigned rest = pairs & ~(1u << p1);
        if (rest) {
            int p2 = highest(rest);
            top_ranks(any & ~(1u << p1) & ~(1u << p2), 1, k);
            return make_score(TWO_PAIR, p1, p2, k[0]);
        }
        top_ranks(any & ~(1u << p1), 3, k);
        return make_score(PAIR, p1, k[0], k[1], k[2]);
    }

    top_ranks(any, 5, k);
    return make_score(HIGH_CARD, k[0], k[1], k[2], k[3], k[4]);
}

Hand_rankings score_category(int score) {
    return static_cast<Hand_rankings>(score >> 20);
}

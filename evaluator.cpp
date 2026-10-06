#include "evaluator.h"

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

static int make_score(Hand_rankings category, const vector<int>& ranks) {
    int score = category;
    for (size_t i = 0; i < 5; i++) {
        score = score * 16 + (i < ranks.size() ? ranks[i] : 0);
    }
    return score;
}

Hand_rankings score_category(int score) {
    return static_cast<Hand_rankings>(score >> 20);
}

static vector<int> top_ranks(const array<int, 13>& count, const vector<int>& exclude, int n) {
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

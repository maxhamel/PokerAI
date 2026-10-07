#include "engine/game.h"
#include "engine/evaluator.h"

void Game::createDeck() {

    deck.reserve(52);
    community.reserve(5);

    for (int s = 0; s < NUMBER_OF_SUITS; s++) {
        for(int r = 0; r < NUMBER_OF_RANKS; r++) {
            Card curr_card{static_cast<Suit>(s), static_cast<Rank>(r)};
            deck.push_back(curr_card);
        }
    }
}

void Game::createPlayers() {
    players.reserve(agents.size());

    for (int i = 0; i < (int)agents.size(); i++) {
        Player curr_player(i);
        players.push_back(curr_player);
    }
}

Game::Game(const vector<Agent*>& agents, GameObserver* observer, bool verbose, unsigned seed)
    : agents(agents), observer(observer), verbose(verbose),
      logging(verbose || observer), rng(seed) {
    createDeck();
    createPlayers();
    last_action.assign(players.size(), "");
}

void Game::log(const string& line) {
    if (!logging) return;
    if (verbose) cout << line << '\n';
    log_lines.push_back(line);
    if (log_lines.size() > 50) log_lines.erase(log_lines.begin());
}

void Game::reset_hand() {
    cards_left = deck.size(); // every card is back in the deck
    community.clear();
    for (Player &p: players) p.new_hand();
    if (logging) last_action.assign(players.size(), "");
    deal_preflop();
}

int Game::live_count() {
    int count = 0;
    for (Player &p: players) if (!p.is_folded()) count++;
    return count;
}

int Game::players_with_chips() {
    int count = 0;
    for (Player &p: players) if (p.get_chips() > 0) count++;
    return count;
}

// Next seat clockwise from `seat` that has chips, skipping busted players.
int Game::next_seat_with_chips(int seat) {
    int n = players.size();
    for (int i = 1; i <= n; i++) {
        int s = (seat + i) % n;
        if (players[s].get_chips() > 0) return s;
    }
    return seat;
}

bool Game::can_act(const Player& p) {
    return !p.is_folded() && !p.is_all_in();
}

void Game::post_blinds(int sb, int bb) {
    pot_size += players[sb].bet(SMALL_BLIND);
    pot_size += players[bb].bet(BIG_BLIND);
    current_bet = BIG_BLIND;
    min_raise = BIG_BLIND;
}

void Game::new_street() {
    for (Player &p: players) p.new_street();
    if (logging) last_action.assign(players.size(), "");
    current_bet = 0;
    min_raise = BIG_BLIND;
}

// Public information only: what anyone watching the table can see.
PlayerView Game::table_view() {
    PlayerView v;
    fill_public(v);
    return v;
}

// Writes the public table state into `v`, reusing its vectors' memory.
void Game::fill_public(PlayerView& v) {
    int n = players.size();
    v.id = -1;
    v.hole.clear();
    v.community = community;
    v.hand_number = hand_counter;
    v.button = button_pos;
    v.small_blind = sb_pos;
    v.big_blind = bb_pos;
    v.street = street;
    v.raises = raises;
    v.last_aggressor = last_aggressor;
    v.pot = pot_size;
    v.to_call = 0;
    v.current_bet = current_bet;
    v.min_raise_to = 0;
    v.max_raise_to = 0;
    v.can_raise = false;
    v.stacks.resize(n);
    v.street_bets.resize(n);
    v.folded.resize(n);
    for (int i = 0; i < n; i++) {
        v.stacks[i] = players[i].get_chips();
        v.street_bets[i] = players[i].get_street_bet();
        v.folded[i] = players[i].is_folded();
    }
    if (logging) { // text is only for display
        v.last_action = last_action;
        int first = max(0, (int)log_lines.size() - 7);
        v.log.assign(log_lines.begin() + first, log_lines.end());
    }
}

// The acting player's view. Reuses one PlayerView so deciding an action
// doesn't allocate memory; it's only valid until the next call.
const PlayerView& Game::make_view(const Player& p, bool can_raise) {
    PlayerView& v = view;
    fill_public(v);
    v.id = p.get_id();
    v.hole = p.get_hand();
    v.to_call = current_bet - p.get_street_bet();
    v.max_raise_to = p.get_street_bet() + p.get_chips();
    v.min_raise_to = min(current_bet + min_raise, v.max_raise_to); // short all-in allowed
    v.can_raise = can_raise && v.max_raise_to > current_bet;
    return v;
}

// Asks the seat's agent (a person or a bot) for an action. It only gets
// what that player can see.
Action Game::get_action(const PlayerView& v) {
    return agents[v.id]->act(v);
}

// Runs one street of betting. Ends once everyone who can act has
// acted since the last raise, or only one player is left.
void Game::betting_round(int first) {
    int n = players.size();
    vector<int>& acted_at = acted_at_buf; // current_bet when each player last acted
    acted_at.assign(n, -1);
    int last_full_raise = current_bet; // bet level set by the last complete raise
    // running counts, updated after each action instead of rescanning the table
    int live = live_count(); // players who haven't folded
    int active = 0;          // players who can still act (not folded or all-in)
    for (Player &p: players) if (can_act(p)) active++;
    int to_act = active;

    for (int i = first % n; to_act > 0 && live > 1; i = (i + 1) % n) {
        Player &p = players[i];
        if (!can_act(p)) continue;

        int to_call = current_bet - p.get_street_bet();
        // nobody left to bet against (everyone else folded or all-in)
        int others = active - 1;
        if (others == 0 && to_call == 0) break;

        // An all-in raise smaller than a full raise doesn't reopen
        // betting for players who already acted.
        bool can_raise = others > 0 && (acted_at[i] == -1 || last_full_raise > acted_at[i]);
        const PlayerView& v = make_view(p, can_raise);
        Action a = get_action(v);
        if (!is_legal(v, a)) a = {to_call == 0 ? CHECK : FOLD};

        bool opening_bet = current_bet == 0;
        if (a.type == FOLD) {
            p.fold();
        } else if (a.type == CALL) {
            pot_size += p.bet(to_call);
        } else if (a.type == RAISE) {
            if (a.amount - current_bet >= min_raise) {
                min_raise = a.amount - current_bet;
                last_full_raise = a.amount;
            }
            current_bet = a.amount;
            pot_size += p.bet(a.amount - p.get_street_bet());
            raises[street]++;
            last_aggressor = i;
        }
        acted_at[i] = current_bet;
        if (a.type == FOLD) { live--; active--; }
        else if (p.is_all_in()) active--;

        if (logging) {
            string did = a.type == FOLD ? "Fold"
                : a.type == CHECK ? "Check"
                : a.type == CALL ? "Call " + to_string(p.get_street_bet())
                : (opening_bet ? "Bet " : "Raise to ") + to_string(a.amount);
            if (p.is_all_in()) did += " (all-in)";
            last_action[i] = did;
            log("Player " + to_string(i + 1) + ": " + did);
        }

        if (a.type == RAISE) {
            // everyone else has to respond to the raise
            to_act = active - (can_act(p) ? 1 : 0);
        } else {
            to_act--;
        }
    }
}

void Game::play_hand() {
    reset_hand();
    hand_counter++;
    if (logging) log("--- Hand #" + to_string(hand_counter) + " ---");
    street = PREFLOP;
    raises.fill(0);
    last_aggressor = -1;

    // Heads-up, the button posts the small blind and acts first preflop.
    sb_pos = live_count() == 2 ? button_pos : next_seat_with_chips(button_pos);
    bb_pos = next_seat_with_chips(sb_pos);
    post_blinds(sb_pos, bb_pos);
    betting_round(bb_pos + 1); // preflop starts left of the big blind

    int cards_per_street[] = {3, 1, 1};
    for (int cards : cards_per_street) {
        if (live_count() <= 1) break;
        deal(cards);
        street = static_cast<Street>(street + 1);
        new_street();
        if (logging) log(community.size() == 3 ? "Flop" : community.size() == 4 ? "Turn" : "River");
        betting_round(button_pos + 1); // postflop starts left of the button
    }

    const vector<int>& won = payouts();
    if (observer) {
        HandResult result;
        result.won = won;
        result.shown.assign(players.size(), {});
        result.hand_name.assign(players.size(), "");
        if (live_count() > 1) { // a hand won by folds isn't shown
            for (Player &p: players) {
                if (p.is_folded()) continue;
                result.shown[p.get_id()] = p.get_hand();
                result.hand_name[p.get_id()] = HAND_NAMES[score_category(evaluate(p.get_hand(), community))];
            }
        }
        observer->hand_over(table_view(), result);
    }
    button_pos = next_seat_with_chips(button_pos);
}

void Game::set_stacks(int stack) {
    for (Player &p: players) p.set_chips(stack);
}

const vector<int>& Game::play_cash_hand(int stack) {
    set_stacks(stack);
    play_hand();
    profit_buf.resize(players.size());
    for (int i = 0; i < (int)players.size(); i++) profit_buf[i] = players[i].get_chips() - stack;
    return profit_buf;
}

// Plays hands until one player has all the chips. Returns the winner's seat.
int Game::play() {
    while (players_with_chips() > 1) play_hand();
    int winner = next_seat_with_chips(-1);
    if (observer) observer->game_over(table_view(), winner);
    return winner;
}

// Deals a random card from the ones not dealt yet this hand. This is a
// Fisher-Yates shuffle done one card at a time, so only the cards actually
// dealt get shuffled.
Card Game::draw() {
    int j = rng.below(cards_left);
    swap(deck[j], deck[cards_left - 1]);
    return deck[--cards_left];
}

void Game::deal_preflop() {
    for (Player &p: players) {
        if (p.is_folded()) continue; // busted players aren't dealt in
        p.deal_card(draw());
        p.deal_card(draw());
    }
}

void Game::deal(int amount) {
    for (int i = 0; i < amount; i++) {
        community.push_back(draw());
    }
}

// Splits the pot into main/side pots by repeatedly peeling off the
// smallest remaining bet among live players.
const vector<int>& Game::payouts() {
    int n = players.size();
    bool showdown = live_count() > 1; // no need to evaluate a hand won by folds
    vector<int>& bets = bets_buf;
    vector<int>& scores = scores_buf;
    vector<int>& won = won_buf;
    vector<int>& winners = winners_buf;
    bets.assign(n, 0);
    scores.assign(n, -1);
    won.assign(n, 0);
    for (Player& p : players) {
        bets[p.get_id()] = p.get_total_bet();
        if (!p.is_folded() && showdown) {
            scores[p.get_id()] = evaluate(p.get_hand(), community);
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
        winners.clear();
        for (Player& p : players) {
            int id = p.get_id();
            if (p.is_folded() || bets[id] == 0) continue;
            if (scores[id] > best) { best = scores[id]; winners.assign(1, id); }
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
        if (logging) log("Player " + to_string(id + 1) + " wins " + to_string(won[id]));
    }
    pot_size = 0;
    return won;
}

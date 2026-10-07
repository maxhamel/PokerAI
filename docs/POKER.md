# Poker engine

No-limit Texas Hold'em for 2–9 players: game rules in `src/engine/`, the
table window in `src/ui/`.

## Game modes

| Mode | Stacks | Used by |
|---|---|---|
| **Tournament** (`Game::play()`) | Everyone starts with 100 chips (blinds 1/2) and plays until one player has all the chips. Busted players sit out. | `./poker`, `hotseat`, `sim` |
| **Cash** (`Game::play_cash_hand(stack)`) | Every hand starts with the same stack, so hands are independent. It returns each seat's profit for the hand. | AI training and `eval` (200 chips = 100 big blinds) |

`./poker play <model>` runs a tournament with 200-chip stacks, because that's
the stack depth the AI was trained at.

## Rules implemented

- **Blinds:** small blind 1, big blind 2. Blinds count toward what the blind
  players owe preflop, and the big blind gets the option to check or raise
  if everyone just calls.
- **Action order:** preflop starts left of the big blind; every later street
  starts left of the button. Folded and all-in players are skipped.
- **Heads-up:** the button posts the small blind, acts first preflop, and
  acts last on every later street.
- **Busted players:** the button and blinds skip players with no chips, and
  those players aren't dealt cards.
- **Betting:** the minimum bet is the big blind. The minimum raise is the size
  of the previous bet or raise. An all-in for less than the minimum is always
  allowed.
- **Incomplete all-in raises:** an all-in raise smaller than a full raise
  doesn't let players who already acted raise again. They can only call or
  fold.
- **Side pots:** the pot is split into layers by how much each player put in,
  and each layer is awarded to the best eligible hand. Chips nobody called
  return to the player who bet them. Folded players' chips count toward the
  pot.
- **Split pots:** when a split doesn't divide evenly, the leftover chips go
  one each to the winners clockwise from the button.
- **Showdown:** each player's best five cards from their seven count, and suits
  never break ties. An ace can be high or low in a straight (A-2-3-4-5). A
  hand won because everyone folded isn't shown.

Not implemented: antes, straddles, limit and pot-limit betting, and rising
blinds. Burn cards and misdeals are also left out; they matter for a physical
dealer, not for a computer shuffle.

## How it fits together

```
Game (engine/game.*)          deals, runs betting rounds, settles pots
  ├─ asks each seat's Agent for an action, passing a PlayerView
  │     ├─ TableUI     (ui/ui.h)        a person at the window
  │     ├─ RandomBot, CallBot, EquityBot, StyleBot,
  │     │  TightAggressiveBot, LooseAggressiveBot (bots/)
  │     └─ NeuralBot   (ai/neural.*)    a trained network
  └─ tells an optional GameObserver (the UI) when hands and games end
```

- **`Agent`** (`engine/agent.h`): anything that can choose an action. It has
  one method, `Action act(const PlayerView&)`. If an agent returns an illegal
  action, the game treats it as a check or fold.
- **`PlayerView`** (`engine/types.h`): everything the acting player is allowed
  to see: their own hole cards, the board, stacks, bets, folds, positions,
  raises per street, and the last aggressor. It never includes other players'
  cards, so a bot can't cheat. To keep the game fast, the view is reused
  between decisions, so an agent must copy anything it wants to keep.
- **`GameObserver`**: receives `hand_over` (with the cards shown at showdown and
  what each player won) and `game_over`. The UI uses these to show results.
- **Hand evaluator** (`engine/evaluator.*`): turns 5–7 cards into a single
  integer score, the hand category followed by up to five ranks in order of
  importance, so comparing scores compares hands. It uses 13-bit masks of
  ranks per suit and finds straights with bit shifts. It never allocates
  memory and runs about 38 million evaluations per second.
- **Randomness** (`engine/rng.h`): `FastRng` (xoshiro128++) with unbiased
  bounded draws. Each `Game` takes a seed, so a seed always replays the same
  games.

## Table UI

`src/ui/ui.h` draws the table with raylib in a 1280×800 window:

- **Seats:** shown around an oval table with each player's chip count, D/SB/BB
  badges, and their last action (or FOLDED / ALL-IN / OUT).
- **Cards:** only the acting player's hole cards are face up. At showdown,
  every hand still in is shown with its name, and winners are highlighted.
- **Center and log:** the pot and board are in the middle, with an action log
  at the bottom left.
- **Actions panel** (bottom right): Fold, Check/Call, raise presets (Min, ½ Pot,
  Pot, All-in), a slider, and a confirm button. Illegal choices are disabled.
- **Keyboard:** F folds, C or K checks or calls, R or Enter raises, and the
  arrow keys or mouse wheel change the raise amount by one big blind.

## Testing

All tests were run during development; none are wired into a test runner yet.

- **Sanitizers:** thousands of complete games with random legal actions at
  9, 3 and 2 players, under AddressSanitizer and the undefined-behaviour
  checker. There were no errors, total chips never changed, and the winner
  always held every chip.
- **Zero-sum cash hands:** 10,000 cash hands with neural, equity and random bots
  were all zero-sum. All 75,187 actions in them were legal, and no bot ever
  folded when it could check for free.
- **Scripted hands:** heads-up action order, short all-in raises not reopening
  betting, side pots, and split pots.
- **Hand evaluator:** matches a straightforward reference implementation on all
  2,598,960 five-card hands and 6 million random 6- and 7-card hands.
- **Dealing:** uniform over 2 million hands (chi-square test on every dealt
  position).
- **UI:** checked with automated screenshots of the action and showdown screens.

## Performance

The bot-only simulation (`./poker sim`) was optimized for training throughput.
Each step was measured as the median of several runs. Unless a step changed
the random number usage, it reproduced a checksum of every game's outcome
exactly.

| Change | Hands/s after | Gain |
|---|---|---|
| Starting point (`-O2`) | 88,300 | — |
| Create the random generator once per game instead of every hand | 95,300 | +8% |
| Skip building log text when nobody is watching | 130,200 | +37% |
| Reuse one `PlayerView` instead of allocating one per action | 338,500 | 2.6× |
| Skip copying display text into the view | 363,400 | +7% |
| Bitmask hand evaluator (16× faster on its own) | 499,200 | +37% |
| Draw cards as needed instead of shuffling the whole deck | 755,300 | +51% |
| `FastRng` for dealing and bots | 874,500 | +16% |
| Reuse temporary vectors in betting and payouts | 1,113,400 | +27% |
| Running counts of live and active players | 1,386,600 | +25% |
| `vector<char>` instead of `vector<bool>` for folds | 1,670,700 | +20% |
| `-O3 -flto` | ~1,680,000 | +3% |
| **All 8 cores** (`sim` runs games in parallel) | **~8,000,000** | 5× |

Two things affect speed more than any code change:

- **Build optimized for training.** The unoptimized debug build is about 10×
  slower.
- **Equity sampling.** The engine now spends about 0.1 µs per action, so a
  neural bot's cost is mostly equity sampling (see
  [AI docs](AI.md#cost)), not the game.

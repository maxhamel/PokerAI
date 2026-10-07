# Poker AI

A small neural network that reads a summary of the table and picks an
action, evolved with a genetic algorithm to maximize chips won per hand. One
model is trained for heads-up and one for 6-max. Code is in `src/ai/`.

## Format

Training and evaluation use **cash-game hands**: every hand starts with
**100 big blinds** (200 chips, blinds 1/2). That makes every hand an
independent sample, so **chips won per hand** is a well-defined measure.
1 chip/hand = 50 bb/100.

The original plan in this README was 9-player tournaments with the bottom 4
players replaced every 100 hands. That changed for two reasons:

- **100 hands of poker is mostly luck.** A single all-in swings 200 chips, so
  rankings over 100 hands mostly reflect the cards dealt.
- **Tournament results depend on stack sizes,** so they're hard to compare
  between hands or between generations.

Heads-up and 6-max are also the standard formats in poker AI research.

## Inputs

`FeatureExtractor` (`ai/features.*`) turns a `PlayerView` into **61 numbers,
all between 0 and 1**, from the acting player's point of view:

- Opponents are listed clockwise from the player.
- Chip amounts are in big blinds or as a share of the pot.

The layout is versioned (`FEATURE_VERSION`). A saved network won't load if
the layout changes.

| Group | Count | Features |
|---|---|---|
| Hand | 13 | equity, hand category (9 one-hot slots), flush draw, straight draw (½ = gutshot, 1 = open-ended), preflop strength (Chen formula) |
| Street | 4 | one-hot: preflop, flop, turn, river |
| Price | 5 | pot odds, stack-to-pot ratio, own stack, call as a share of own stack, pot size |
| Position | 3 | share of opponents acting after us (0 = button), opponents in the hand, opponents who can still bet |
| History | 4 | raises this street, raises last street, we were the last aggressor, someone else was |
| Opponents | 8 × 4 | for each seat clockwise: in hand, all-in, stack, bet this street |

**Equity** is the chance of winning at showdown. It's estimated by simulation:
deal the opponents random hands and complete the board at random, many
times, and count wins. Ties count as a share of a win.

Checks on the features:

- **Exact equity:** compared against exact answers, found by enumerating
  every possible card and opponent hand on the turn and river (including a
  two-opponent case). Every estimate was within 0.0025.
- **Preflop odds** against one random hand:

  | Hand | Measured | Published |
  |---|---|---|
  | AA | 0.851 | 0.852 |
  | KK | 0.825 | 0.824 |
  | AKs | 0.670 | 0.670 |
  | 72o | 0.347 | 0.346 |

- **Ranges:** across 19,073 decisions from real games, every feature stayed
  between 0 and 1, and each one-hot group had exactly one slot set.

`./poker features` prints the vector at each decision during one hand.

### Cost

Equity simulation dominates the cost of a decision:

| Equity samples | 1 opponent | 8 opponents |
|---|---|---|
| 50 | 3 µs | 6 µs |
| 200 | 12 µs | 23 µs |
| 1,000 | 58 µs | 114 µs |

Training uses 32 samples, which is noisier but fast. `play` uses 100.

## Network and actions

`Network` (`ai/neural.*`) is fully connected: 61 inputs → 32 tanh → 16 tanh →
6 scores, about 2,600 weights in total.

The six choices are **fold, check/call, raise ½ pot, raise pot, raise 2× pot,
all-in**. Raise sizes are clamped to what's legal.

To turn the scores into an action:

1. Illegal choices are removed. The bot never folds when it could check for
   free.
2. The action is drawn at random, weighted by `softmax(score / temperature)`.
   Picking randomly rather than always the top score keeps the bot from being
   predictable.

The temperature is saved with the network. Models are plain-text files with
a header (format, feature version, table size, temperature, layer sizes)
followed by the weights. A file with the wrong layout won't load.

## Training

`train()` (`ai/train.*`) runs a genetic algorithm, training against a mix of
fixed bots and a **pool of its own past versions**:

1. **Start:** 48 random networks.
2. **Evaluate:** each generation, every network plays **1,600 cash hands**,
   and its fitness is its **average profit per hand**. Opponents change every
   50 hands. By default each seat is drawn from:
   - 40% past versions from the pool
   - 20% tight StyleBots
   - 20% loose-aggressive StyleBots
   - 20% RandomBots

   Until the pool has a version, those seats go to the other bots instead.
3. **Same conditions for everyone:** every network in a generation gets the
   same deals and the same opponents, so the comparison between networks is
   fair.
4. **Next generation:** the top 6 networks are kept unchanged. The other 42
   are mutated children: a parent is picked by tournament (the best of 3
   chosen at random), then each weight has a 10% chance of a Gaussian change
   with standard deviation 0.1.
5. **Pool check:** every 10 generations, the current best network plays 3,000
   new hands against a table of each version in the pool.
   - It **joins the pool only if it beats the pool overall with 95%
     confidence.**
   - The pool holds 8 versions. When it's full, a new version replaces the
     **oldest** one.
   - The first champion joins automatically.
6. **Saving:** each version that joins is saved to
   `models/<table>_versions/vNNNN.net`, where NNNN is its generation, and also
   becomes the main model at `models/<table>.net`. Each check also prints the
   champion's result against the fixed bots in the mix, for monitoring.

Replacing the version that the champion beat by the largest margin was tried
first. It kept the earliest versions forever: they fold almost everything, so
every champion beats them by only a small margin, and they never get
replaced. Replacing the oldest version fixed that.

Networks are evaluated on all CPU cores. On an Apple M3, 300 generations take
about 1 minute heads-up and 2 minutes for 6-max.

```sh
./poker train headsup gens=300                 # -> models/headsup.net
./poker train 6max gens=300 out=models/new.net # don't overwrite the committed model
```

| Option | Default | Meaning |
|---|---|---|
| `gens` | 100 | generations |
| `pop` | 48 | networks per generation |
| `hands` | 1600 | hands per network per generation |
| `block` | 50 | hands before the opponents change |
| `elites` | 6 | networks copied unchanged |
| `rate`, `size` | 0.1, 0.1 | mutation chance per weight, and its size |
| `samples` | 32 | equity samples per decision |
| `temp` | 0.25 | action temperature |
| `versions`, `tight`, `loose`, `random`, `equity`, `call` | 40, 20, 20, 20, 0, 0 | opponent mix weights |
| `pool`, `pool_hands` | 8, 3000 | pool size, and hands against each version in a pool check |
| `eval_every`, `eval_hands` | 10, 20000 | generations between pool checks, and hands for the fixed-bot printout |
| `seed`, `threads`, `out` | 1, all cores, `models/<table>.net` | |

Training is deterministic for a given seed and build.

## Evaluation

`./poker eval <model> [hands] [opponent model]` plays the model against a
full table of each bot type for 100,000 hands by default. It reports
chips/hand with a 95% confidence interval, and uses a fixed seed, so results
are reproducible. With a second model, it also plays head-to-head against a
table full of copies of that model.

| Bot | Style | Role |
|---|---|---|
| `StyleBot` | equity-based, with a style set by five parameters: how loosely it calls, how thin it bets, how often it bets when it could, its bluff rate, and its bet size. Random styles cover everything from rocks to calling stations and maniacs; `Style::tight` and `Style::loose_aggressive` draw from those two corners. | training (tight and loose) |
| `RandomBot` | random legal actions, mostly calls | training |
| `EquityBot` | bets when its equity is high, calls when the pot odds justify it, otherwise folds; very passive preflop | earlier training mix |
| `CallBot` | always checks or calls | earlier training mix |
| `TightAggressiveBot` | hand-written: picks starting hands by Chen score, bets made hands, chases draws only at good prices, bluffs occasionally | **held out** |
| `LooseAggressiveBot` | hand-written: plays most hands, raises often, bets any pair or draw, bluffs a lot | **held out** |

**The held-out bots are never used in training or in picking a model.** They
test whether the AI learned to play tight and loose styles in general, or
only the bots it practiced against. Their styles overlap the training
StyleBots, but the code behind them is completely different.

Measured play style over 6-max hands:
- **VPIP:** how often a player voluntarily puts chips in preflop.
- **PFR:** how often a player raises preflop.
- **AF:** bets and raises divided by calls, after the flop.

| Bot | VPIP | PFR | AF |
|---|---|---|---|
| tight StyleBots | 10–18% | 1–5% | 0.8–3.5 |
| loose-aggressive StyleBots | 32–54% | 13–26% | 1.4–3.0 |
| random-style StyleBots | 15–50% | 2–36% | 0.5–10 |
| EquityBot | 19% | 2% | 1.2 |
| TightAggressiveBot (held out) | 15% | 4% | 2.7 |
| LooseAggressiveBot (held out) | 60% | 13% | 4.8 |

For scale, here is how simple strategies do against a table of EquityBots
(chips/hand):

| Strategy | Heads-up | 6-max |
|---|---|---|
| EquityBot (itself) | +0.2 | -0.1 |
| Fold every hand | -0.6 | -0.2 |
| TightAggressiveBot | -2.8 | -0.4 |
| CallBot | -5.9 | -23.0 |
| RandomBot | -11.2 | -19.5 |
| Untrained random network | -18.6 | -25.2 |

## Findings

### 1. First attempt: worse than folding every hand

The first version trained at temperature 1, with half the opponents being
other networks from the current population, and 400 hands per network.
After 300 generations, the best networks lost **-8.6 (heads-up) and -19.8
(6-max) chips/hand** to EquityBots. That's far worse than folding every
hand. The 6-max model also got *worse* over time, ending around -40, even
though its fitness kept going up.

There were two causes:

1. **Temperature 1 made networks play almost randomly.** Random starting
   weights produce scores that are close together, so the softmax gives each
   action about a 1/6 chance. Small mutations couldn't make the scores
   different enough for the network to be decisive.
2. **Drift from playing the current population.** With half the opponents
   being other weak networks, the population learned to beat itself instead
   of learning good poker.

### 2. Fixing the basics

These were heads-up runs of 150 generations, scored by the best check
against EquityBots (each ±1.6 chips/hand):

| Setting | vs EquityBots |
|---|---|
| Original (temp 1, 50% population self-play, 400 hands) | -11.7 |
| Temperature 0.25 | -7.0 |
| No self-play | -6.2 |
| **Temperature 0.25 + no self-play** | **+2.7** |
| … + mutation size 0.3 | +5.4 |
| … + 1,600 hands per network | **+6.7** |
| … + mutation size 0.3 and 1,600 hands | +5.9 |
| … + temperature 0.1 | +6.1 |
| … + 25% self-play | +5.5 |
| … + mutation rate 0.3 | +5.6 |

What mattered:

- **Lowering the temperature together with dropping self-play** fixed the
  failure, taking heads-up from losing to winning.
- **More hands per network** helped next, because fitness is less noisy.
- **Everything else was within noise.**

The **committed models** in `models/` were trained this way: 300
generations, temperature 0.25, 1,600 hands, against 60% EquityBots, 20%
CallBots and 20% RandomBots, with the network saved by its EquityBot check.
That trainer has since been replaced by the version pool.

### 3. The committed models exploit their training opponents

Committed models, 50,000 hands per table, chips/hand:

| Opponents | Heads-up | 6-max |
|---|---|---|
| tight StyleBots | +2.8 ± 0.5 | -2.3 ± 0.6 |
| loose StyleBots | +7.3 ± 0.6 | -13.0 ± 0.8 |
| RandomBots | +20.2 ± 0.8 | +27.4 ± 1.9 |
| CallBots | +48.0 ± 1.1 | +91.8 ± 3.5 |
| EquityBots | +7.0 ± 0.6 | +2.0 ± 0.7 |
| TightAggressiveBots (held out) | -0.2 ± 0.1 | -4.0 ± 0.4 |
| LooseAggressiveBots (held out) | +4.4 ± 0.5 | -11.8 ± 1.2 |
| **Held-out average** | **+2.1** | **-7.9** |

They beat the bots they trained against, but **the 6-max model loses badly
to loose-aggressive play** of either kind (StyleBots and held-out alike). It
learned to shove all-in for 100 big blinds from early position, which works
against EquityBots (very passive preflop) and CallBots, but not against
players who fight back.

### 4. Varied opponents and a pool of past versions

Two ways of varying the opponents were compared on the same tests, each from
a single training run of 300 generations:

- **70% StyleBots:** 70% random-style StyleBots, with the rest from the old
  mix. This was an earlier experiment, and its option has since been
  replaced.
- **Version pool:** the current default trainer (see [Training](#training)).

| | Committed | 70% StyleBots | **Version pool** |
|---|---|---|---|
| **6-max**, held-out average | -7.9 | +2.5 | -0.5 |
| 6-max, this model at a table of committed models | — | +20.1 ± 1.7 | +8.2 ± 0.9 |
| 6-max, version-pool model at a table of this model | +8.2 ± 0.9 | +3.7 ± 1.2 | — |
| 6-max, vs CallBots | +91.8 | +50.2 | +8.5 |
| **Heads-up**, held-out average | +2.1 | +4.2 | +3.4 |
| Heads-up, this model vs the committed model | — | -36.8 ± 0.8 | ±0.0 |
| Heads-up, version-pool model vs this model | ±0.0 | +30.6 ± 0.8 | — |
| Heads-up, vs CallBots | +48.0 | +45.9 | +41.5 |

In 6-max, one model at a table of five copies of another isn't the mirror
image of the reverse matchup, so only the measured direction is shown.

For 6-max, StyleBot-only training (100% StyleBots) was also tried. It
collapsed into folding nearly everything, scoring about 0 against every
opponent, including RandomBots and CallBots.

What this shows:

- **Varied opponents fixed the 6-max model's worst weakness.** Its held-out
  average went from -7.9 to about breakeven or better.
- **The version pool produces the hardest-to-exploit AIs.** Its models win or
  tie every head-to-head matchup measured. The 70%-StyleBots heads-up model
  scores well against fixed bots but loses heavily to both other heads-up AIs
  (-31 to -37 chips/hand).
- **That robustness costs exploitation.** The pool-trained 6-max AI wins only
  +8.5/hand against CallBots, compared with +92 for the committed model.
  Adding EquityBots and CallBots back (30% versions, 20% tight, 20% loose,
  10% random, 10% equity, 10% call) won more from CallBots (+46.7) and beat
  the 70% model head-to-head (+8.6 ± 1.3). But it only tied the default
  pool model (-0.4 ± 0.4) and did worse against the held-out bots (-3.1).
- **6-max results are noisy.** Each number comes from one training run.
  Differences of a few chips/hand between 6-max approaches could change with
  another seed.

To reproduce the version-pool models (deterministic with the default seed):

```sh
./poker train headsup gens=300 out=models/pool/headsup.net
./poker train 6max gens=300 out=models/pool/6max.net
./poker eval models/pool/6max.net 50000 models/6max.net   # includes head-to-head vs committed
```

**Status:** `models/` still holds the earlier committed models. The
version-pool 6-max model is the recommended replacement: it's better against
the held-out bots by 7.4 chips/hand, and wins +8.2 chips/hand at a table of
committed models. For heads-up, the pool model ties the committed one
head-to-head and does a little better against the held-out bots, but wins
less from EquityBots and CallBots.

For context, the bb/100 numbers look large only because the opponents are
weak. Research AIs beat Slumbot, a strong heads-up bot, by roughly 10–75
bb/100, and win rates between strong players are that small. Beating simple
rule bots is a start, not strong play.

## Next steps

- **Robust and exploitative at once.** Pool-trained AIs are hard to exploit
  but passive against weak players. Possible fixes:
  - Weight fitness toward fixed weak opponents.
  - Keep a separate "exploiter" network.
  - Give the network opponent statistics (how often each player calls or
    raises), so one model can adapt to its table.
- **Multiple seeds.** Train each setting several times and compare averages,
  especially for 6-max.
- **Resume training from the pool.** Start a new run from saved versions
  instead of random networks.
- **Duplicate evaluation.** Replaying the same deals with the seats rotated
  cancels out card luck. Seeded games make this straightforward.
- **Opponent ranges for equity.** Equity currently assumes opponents hold
  random hands, but someone who has raised three times usually holds a strong
  hand.
- **Stronger outside benchmarks.** Play heads-up against
  [Slumbot](https://github.com/aipoker-bot/slumbot-adapter) through its API,
  or check exploitability on small games (Kuhn or Leduc poker) with
  [OpenSpiel](https://arxiv.org/pdf/1908.09453).
- **Other training methods.** Genetic algorithms stop scaling beyond small
  networks. Counterfactual regret minimization (CFR) and reinforcement
  learning are what state-of-the-art poker AIs use.

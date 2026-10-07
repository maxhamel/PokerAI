# PokerAI

A Texas Hold'em engine in C++ and an AI trained to play it, with a raylib
table to play against it.

- **[Poker engine docs](docs/POKER.md):** rules, game flow, the table UI,
  testing, and performance
- **[AI docs](docs/AI.md):** inputs, network, training, experiments, and
  results

## Results

Two networks were trained with a genetic algorithm to maximize chips won per
hand: one for **heads-up** (2 players) and one for **6-max** (6 players).
These are the models committed in `models/`. Each was played for 50,000 cash
hands per opponent type, with 100 big blind stacks and 1/2 blinds. Numbers are
chips won per hand (± 95% confidence interval):

| Opponents | Heads-up AI | 6-max AI |
|---|---|---|
| RandomBots | +20.2 ± 0.8 | +27.4 ± 1.9 |
| CallBots | +48.0 ± 1.1 | +91.8 ± 3.5 |
| EquityBots | **+7.0 ± 0.6** | **+2.0 ± 0.7** |
| TightAggressiveBots *(held out)* | -0.2 ± 0.1 | -4.0 ± 0.4 |
| LooseAggressiveBots *(held out)* | +4.4 ± 0.5 | -11.8 ± 1.2 |

"Held out" means these hand-written bots are never used in training, so they
test whether the AI learned poker in general or only the bots it trained
against. The committed models beat their training opponents but don't
handle new styles well. The 6-max model loses badly to loose-aggressive
play.

**Current progress:** training now uses a **pool of the AI's own past
versions** plus tight, loose-aggressive and random players. A new version
joins the pool only once it beats the versions already there. That 6-max
model:
- improves the held-out average from **-7.9 to -0.5** chips/hand;
- wins **+8.2** chips/hand at a table of committed 6-max models;
- is harder to exploit, but wins less from weak players.

It hasn't replaced the committed model yet. The full experiments are in
[docs/AI.md](docs/AI.md#findings).

## Quick start

Requires macOS with Homebrew and clang (C++20).

```sh
brew install raylib
clang++ -std=c++20 -O3 -flto -Isrc -I/opt/homebrew/include \
    src/main.cpp src/engine/*.cpp src/bots/*.cpp src/ai/*.cpp \
    -L/opt/homebrew/lib -lraylib -o poker
```

In VS Code, **Cmd+Shift+B** builds a debug version. Use
**Terminal → Run Build Task → "Build poker (release, for training)"** for an
optimized build, which is about 10× faster.

| Command | What it does |
|---|---|
| `./poker` | You (Player 1) against 8 RandomBots in the table window |
| `./poker play models/headsup.net` | You against a trained AI, at the table size it was trained for |
| `./poker hotseat` | All 9 seats are human, taking turns on one screen |
| `./poker train <6max\|headsup> [option=value ...]` | Trains a new network ([options](docs/AI.md#training)) |
| `./poker eval <model> [hands] [opponent model]` | Plays a model against each kind of bot (and optionally head-to-head against another model) and reports chips/hand |
| `./poker sim [games] [seed] [threads]` | Bot-only tournaments with no window, for benchmarking the engine |
| `./poker features [seed]` | Prints the AI's input vector at each decision during one hand |

## Project layout

```
src/
  main.cpp       command-line modes
  engine/        game rules: cards, betting, side pots, hand evaluator
  bots/          rule-based bots: Random, Call, Equity, Style, TightAggressive, LooseAggressive
  ai/            feature extractor, neural network, genetic-algorithm trainer
  ui/            raylib table
models/          trained networks (headsup.net, 6max.net); training also writes a
               pool of past versions to models/<table>_versions/
docs/            engine and AI documentation
```

# Summary

The goal by the end of the project is to train an AI to be really good at poker

# ♠️ Poker Game

A Texas Hold'em style game for **up to 9 players**. Each player is dealt **2 hole cards** and starts with a standard amount of chips.

## 🃏 Rounds of Play

### 1. Preflop Betting
- Cards are dealt starting with the **Small Blind (SB)**
- No community cards are shown
- **SB** and **Big Blind (BB)** post the blinds
  - If either player cannot cover their blind, they are forced **all in**
- Action begins **Under the Gun (UTG)**

### 2. Flop Betting
- **3 community cards** are revealed
- Action begins with the **Small Blind**

### 3. Turn Betting
- **1 additional** community card is revealed

### 4. River Betting
- **1 additional** community card is revealed

### 5. Showdown
- Winner is determined by **hand rankings**
- The pot is **chopped** if the best hands are of equal strength
- The pot may be **split into side pots** if some players are all in

---

## 🎯 Actions

| Action | Description |
|--------|-------------|
| **Fold** | Player forfeits the hand |
| **Check** | Pass action without betting; only allowed if no bet has been made |
| **Call** | Player matches the current bet |
| **Bet** | First wager in a betting round (occurs after preflop) |
| **Raise** | Player increases the current bet; must be **at least 2x** |
| **All In** | Player puts all of their remaining chips in the pot |

### All-In Rules
- If a call forces a player all in, the pot is **split** into side pots
- All-in raises do **not** need to meet the 2x minimum
- A player who loses all their chips is **eliminated**

### End of a Betting Round
A betting round continues until either:
- Only **one player** remains in the hand, or
- Every remaining player has **matched the bet** or is **all in**

### Winning the Hand
The winner is either the **last player remaining** in the pot or the best hand at **showdown**.

---

## 🏆 Hand Rankings

*(Highest to lowest)*

1. **Straight Flush**
2. **Four of a Kind** (Quads)
3. **Full House**
4. **Flush**
5. **Straight**
6. **Three of a Kind** (Trips)
7. **Two Pair**
8. **One Pair**
9. **High Card**

---

## 🧱 Object-Oriented Design

### `Game` Class
| Attribute | Description |
|-----------|-------------|
| `players` | List of players |
| `deck` | List of cards remaining |
| `round` | Current betting round |
| `bb_position` | Big Blind position |
| `pot` | Current pot size |
| `hand_counter` | Number of hands played (used for training) |

### `Player` Class
| Attribute | Description |
|-----------|-------------|
| `chips` | Chip count |
| `position` | Seat position at the table |
| `cards` | Player's hole cards |

### `Card` Enums
- **Ranks:** `1, 2, 3, 4, 5, 6, 7, 8, 9, T, J, Q, K, A`
- **Suits:** `Heart, Diamond, Spade, Club`

---

## 🤖 AI Learning

### Training Structure
- Runs as **9-player tournaments**
- After every **100 hands**, the **bottom 4 players** are replaced by **mutations**

### Inputs (Information)
- Own stack size
- Opponents' stack sizes
- Position
- Hand (hole cards)
- Community cards
- Likelihood of hitting each hand
- Bet history

### Outputs (Actions)
- Fold
- Check
- Call
- Bet
- Raise
- All In

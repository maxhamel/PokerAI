#pragma once

#include "agent.h"

// Picks a random legal action. A baseline to test against until the real AI
// exists.
class RandomBot : public Agent {
    private:
        mt19937 rng;

    public:
        explicit RandomBot(unsigned seed) : rng(seed) {}
        Action act(const PlayerView& v) override;
};

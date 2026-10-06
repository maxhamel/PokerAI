#pragma once

#include "agent.h"
#include "rng.h"

// Picks a random legal action. A baseline to test against until the real AI
// exists.
class RandomBot : public Agent {
    private:
        FastRng rng;

    public:
        explicit RandomBot(unsigned seed) : rng(seed) {}
        Action act(const PlayerView& v) override;
};

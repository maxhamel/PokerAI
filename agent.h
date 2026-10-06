#pragma once

#include "types.h"

// Decides the actions for a seat: a person using the UI, or a bot.
class Agent {
    public:
        virtual ~Agent() = default;

        // Called when it's this seat's turn. Should return a legal action
        // (see is_legal); an illegal one is treated as check/fold.
        virtual Action act(const PlayerView& v) = 0;
};

// Gets told about game events that aren't decisions, e.g. so the UI can
// show them. Both methods do nothing unless overridden.
class GameObserver {
    public:
        virtual ~GameObserver() = default;
        virtual void hand_over(const PlayerView& table, const HandResult& result) {}
        virtual void game_over(const PlayerView& table, int winner) {}
};

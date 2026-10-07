#pragma once

#include "engine/agent.h"
#include "ai/features.h"
#include "engine/rng.h"

// What the network can choose. Raises are sized relative to the pot and then
// clamped to what's legal.
enum NetAction {
    NA_FOLD,
    NA_CHECK_CALL,
    NA_RAISE_HALF_POT,
    NA_RAISE_POT,
    NA_RAISE_2X_POT,
    NA_ALL_IN,
    NET_OUTPUTS
};

using NetOutput = array<float, NET_OUTPUTS>;

// A small fully connected network:
//   FEATURE_COUNT inputs -> H1 tanh -> H2 tanh -> NET_OUTPUTS scores
// Stored as one flat weight vector so a genetic algorithm can mutate it easily.
struct Network {
    static constexpr int H1 = 32;
    static constexpr int H2 = 16;
    static constexpr int WEIGHT_COUNT =
        (FEATURE_COUNT + 1) * H1 + (H1 + 1) * H2 + (H2 + 1) * NET_OUTPUTS; // +1 = bias

    vector<float> w = vector<float>(WEIGHT_COUNT, 0.0f);
    int players = 6;           // table size this network was trained for
    float temperature = 1.0f;  // randomness when picking an action (see choose_action)

    // Small random weights, scaled to each layer's number of inputs.
    static Network random(FastRng& rng, int players);

    void forward(const Features& in, NetOutput& out) const;

    // Plain text: a header (format, feature version, table size, layer
    // sizes) followed by the weights. load() rejects files whose layout
    // doesn't match this build.
    bool save(const string& path) const;
    static bool load(const string& path, Network& out, string& error);
};

// Turns the network's scores into a legal action: illegal choices are masked
// out, then one is picked at random weighted by softmax(score / temperature).
// Picking randomly (rather than always the top score) keeps the bot from
// being predictable. Temperature near 0 = nearly always the top choice.
Action choose_action(const PlayerView& v, const NetOutput& scores, FastRng& rng, float temperature);

class NeuralBot : public Agent {
    private:
        const Network* net; // not owned; must outlive the bot
        FeatureExtractor fx;
        FastRng rng;
        Features features;
        NetOutput scores;

    public:
        NeuralBot(const Network* net, uint64_t seed, int equity_samples = 50);
        Action act(const PlayerView& v) override;
};

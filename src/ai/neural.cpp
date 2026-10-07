#include "ai/neural.h"
#include <cmath>
#include <fstream>

static constexpr const char* FILE_MAGIC = "pokerai-network";
static constexpr int FILE_FORMAT = 2;

Network Network::random(FastRng& rng, int players) {
    Network net;
    net.players = players;
    normal_distribution<float> normal(0.0f, 1.0f);
    int sizes[] = {FEATURE_COUNT, H1, H2, NET_OUTPUTS};
    float* w = net.w.data();
    for (int layer = 0; layer < 3; layer++) {
        float scale = 1.0f / sqrtf((float)sizes[layer]);
        for (int o = 0; o < sizes[layer + 1]; o++) {
            for (int i = 0; i < sizes[layer]; i++) *w++ = normal(rng) * scale;
            *w++ = 0; // bias
        }
    }
    return net;
}

// out[o] = bias + sum(in[i] * weight), weights stored output by output with
// the bias last. Returns a pointer just past this layer's weights.
static const float* dense(const float* w, const float* in, int n_in, float* out, int n_out, bool activate) {
    for (int o = 0; o < n_out; o++) {
        float sum = 0;
        for (int i = 0; i < n_in; i++) sum += in[i] * w[i];
        sum += w[n_in];
        w += n_in + 1;
        out[o] = activate ? tanhf(sum) : sum;
    }
    return w;
}

void Network::forward(const Features& in, NetOutput& out) const {
    float h1[H1], h2[H2];
    const float* p = w.data();
    p = dense(p, in.data(), FEATURE_COUNT, h1, H1, true);
    p = dense(p, h1, H1, h2, H2, true);
    dense(p, h2, H2, out.data(), NET_OUTPUTS, false);
}

bool Network::save(const string& path) const {
    ofstream f(path);
    if (!f) return false;
    f << FILE_MAGIC << ' ' << FILE_FORMAT << '\n'
      << "feature_version " << FEATURE_VERSION << '\n'
      << "players " << players << '\n'
      << "temperature " << temperature << '\n'
      << "layers " << FEATURE_COUNT << ' ' << H1 << ' ' << H2 << ' ' << NET_OUTPUTS << '\n';
    f.precision(9);
    for (int i = 0; i < WEIGHT_COUNT; i++) f << w[i] << ((i + 1) % 16 == 0 ? '\n' : ' ');
    f << '\n';
    return (bool)f;
}

bool Network::load(const string& path, Network& out, string& error) {
    ifstream f(path);
    if (!f) { error = "can't open " + path; return false; }
    string magic, key;
    int format, feature_version, players, l0, l1, l2, l3;
    float temperature;
    f >> magic >> format >> key >> feature_version >> key >> players >> key >> temperature
      >> key >> l0 >> l1 >> l2 >> l3;
    if (!f || magic != FILE_MAGIC || format != FILE_FORMAT) { error = path + " isn't a network file"; return false; }
    if (feature_version != FEATURE_VERSION || l0 != FEATURE_COUNT || l1 != H1 || l2 != H2 || l3 != NET_OUTPUTS) {
        error = path + " was trained with a different feature/network layout";
        return false;
    }
    out.players = players;
    out.temperature = temperature;
    for (float& x : out.w) f >> x;
    if (!f) { error = path + " is missing weights"; return false; }
    return true;
}

Action choose_action(const PlayerView& v, const NetOutput& scores, FastRng& rng, float temperature) {
    // what each choice would actually do
    float pot_after_call = v.pot + v.to_call;
    auto raise_to = [&](float pot_fraction) {
        int to = v.current_bet + (int)lroundf(pot_after_call * pot_fraction);
        return max(v.min_raise_to, min(v.max_raise_to, to));
    };
    Action actions[NET_OUTPUTS] = {
        {FOLD},
        {v.to_call > 0 ? CALL : CHECK},
        {RAISE, raise_to(0.5f)},
        {RAISE, raise_to(1.0f)},
        {RAISE, raise_to(2.0f)},
        {RAISE, v.max_raise_to},
    };
    bool legal[NET_OUTPUTS];
    for (int i = 0; i < NET_OUTPUTS; i++) legal[i] = is_legal(v, actions[i]);
    if (v.to_call == 0) legal[NA_FOLD] = false; // never fold when checking is free

    // softmax over the legal choices
    float best = -INFINITY;
    for (int i = 0; i < NET_OUTPUTS; i++) if (legal[i]) best = max(best, scores[i]);
    float t = max(temperature, 1e-3f);
    float weight[NET_OUTPUTS], total = 0;
    for (int i = 0; i < NET_OUTPUTS; i++) {
        weight[i] = legal[i] ? expf((scores[i] - best) / t) : 0;
        total += weight[i];
    }
    float pick = (rng() / 4294967296.0f) * total;
    for (int i = 0; i < NET_OUTPUTS; i++) {
        if (!legal[i]) continue;
        if (pick < weight[i]) return actions[i];
        pick -= weight[i];
    }
    return actions[NA_CHECK_CALL]; // only reached through rounding
}

NeuralBot::NeuralBot(const Network* net, uint64_t seed, int equity_samples)
    : net(net), fx(seed, equity_samples), rng(seed ^ 0x5bd1e995) {}

Action NeuralBot::act(const PlayerView& v) {
    fx.extract(v, features);
    net->forward(features, scores);
    return choose_action(v, scores, rng, net->temperature);
}

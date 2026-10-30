// Runs the C++ player on a message script and writes its output as raw float32 stereo (interleaved).
//   ElixirDesktop_Compare script.json out.f32
// script: { "rate": 48000, "blocks": [ { "msgs": [ ...page messages... ], "frames": 512 }, ... ] }
#include <juce_core/juce_core.h>
#include "Msg.h"
#include <memory>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: ElixirDesktop_Compare script.json out.f32\n"); return 2; }
    const juce::var script = juce::JSON::parse(juce::File(juce::String(argv[1])).loadFileAsString());
    auto player = std::make_unique<lxr::Player>((double) script["rate"]);
    auto msg = std::make_unique<lxr::Msg>();
    const juce::File outFile { juce::String(argv[2]) }; juce::FileOutputStream out { outFile };
    out.setPosition(0); out.truncate();
    std::vector<float> L, R;
    for (const auto& b : *script["blocks"].getArray()) {
        if (auto* ms = b["msgs"].getArray()) for (const auto& m : *ms) if (lxr::parseMsg(m, *msg)) player->msg(*msg);
        const int n = (int) b["frames"];
        L.assign((size_t) n, 0.0f); R.assign((size_t) n, 0.0f);
        player->process(L.data(), R.data(), n);
        for (int i = 0; i < n; i++) { out.writeFloat(L[(size_t) i]); out.writeFloat(R[(size_t) i]); }
        player->nEvents = 0;
    }
    return 0;
}

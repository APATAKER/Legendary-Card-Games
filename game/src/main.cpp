#include "app/App.h"
#include "game/Verify.h"

#include <SDL3/SDL_main.h>

#ifdef _WIN32
extern "C" {
// Prefer the discrete GPU (e.g. NVIDIA RTX) on hybrid laptops.
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#include <cstring>

int main(int argc, char* argv[]) {
    int maxFrames = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            maxFrames = 15;
        } else if (std::strcmp(argv[i], "--verify-mechanics") == 0) {
            std::filesystem::path report = "logs/verify_mechanics.txt";
            if (i + 1 < argc) report = argv[++i];
            return lc::runMechanicsVerification(report);
        }
    }
    lc::App app;
    if (!app.init()) return 1;
    return app.run(maxFrames);
}

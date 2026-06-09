#include "PulsePadWindow.h"
#include "AudioEngine.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string audioInitError;
    if (!pulsepad::AudioEngine::initialize(&argc, &argv, &audioInitError)) {
        std::cerr << audioInitError << std::endl;
        return 1;
    }
    return run_pulsepad_app(argc, argv);
}

#include "sound_mixer.h"
#include "sound_music.h"
#include "xwb_reader.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
unsigned checks = 0;
void require(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::cerr << "FAILED: " << message << std::endl;
    throw std::runtime_error(message);
  }
}
} // namespace

int main(int argc, char **argv) {
  try {
    const std::string xwbPath = (argc > 1) ? argv[1] : "mods/remake_soundtrack/bio4bgm.xwb";
    std::cout << "Testing XwbStreamReader with: " << xwbPath << std::endl;

    // 1. Initial unconfigured state
    require(!re4::XwbStreamReader::IsAvailable(), "Unconfigured reader must not be available");
    require(re4::XwbStreamReader::TrackCount() == 0, "Initial track count must be 0");
    require(re4::XwbStreamReader::LoadTrack(0) == nullptr, "Unconfigured LoadTrack must return null");

    // 2. Configure path
    const bool ok = re4::XwbStreamReader::Configure(xwbPath);
    require(ok, "Configure must succeed on valid bio4bgm.xwb");
    require(re4::XwbStreamReader::IsAvailable(), "Reader must be available after valid config");
    require(re4::XwbStreamReader::IsEnabled(), "Reader must be enabled by default after config");
    require(re4::XwbStreamReader::TrackCount() == 78, "bio4bgm.xwb must have exactly 78 tracks");

    // 3. Load Track 0 (Death / Game Over)
    auto track0 = re4::XwbStreamReader::LoadTrack(0);
    require(track0 != nullptr, "Track 0 (Death) must load");
    require(track0->rate == 48000, "Track 0 rate must be 48000 Hz");
    require(track0->stereo, "Track 0 must be stereo");
    require(track0->left.size() > 1000, "Track 0 left channel must contain PCM samples");
    require(track0->left.size() == track0->right.size(), "Left and Right channel lengths must match");

    // 4. Load Track 2 (Village Combat Theme)
    auto track2 = re4::XwbStreamReader::LoadTrack(2);
    require(track2 != nullptr, "Track 2 (Combat) must load");
    require(track2->rate == 48000, "Track 2 rate must be 48000 Hz");
    require(track2->stereo, "Track 2 must be stereo");
    require(track2->looping, "Combat track must loop");
    require(track2->loopLeft.size() == track2->left.size(), "Combat loop size must match full sample length");

    // Check that samples are non-zero
    const bool hasSound = std::any_of(track2->left.begin(), track2->left.end(), [](std::int16_t s) { return s != 0; });
    require(hasSound, "Track 2 must have audible non-zero waveform samples");

    // 5. Integrate into SoundMixer
    re4::SoundMixer mixer;
    mixer.installStreamAssets({track2}, true);
    mixer.setPaused(false);
    const auto handle = mixer.requestStream(0, 2, 0x80000003);
    require(handle != 0, "Mixer must accept Remake combat stream");

    std::vector<std::int16_t> rendered(480 * 2);
    mixer.render(rendered.data(), 480);
    require(mixer.streamRenderedFrames(0, 2) == 480, "Mixer must render 480 frames from Remake PCM");

    // 6. Test Enable/Disable toggle
    re4::XwbStreamReader::SetEnabled(false);
    require(!re4::XwbStreamReader::IsEnabled(), "SetEnabled(false) must update IsEnabled()");
    re4::XwbStreamReader::SetEnabled(true);
    require(re4::XwbStreamReader::IsEnabled(), "SetEnabled(true) must restore IsEnabled()");

    // 7. Bounds check
    require(re4::XwbStreamReader::LoadTrack(78) == nullptr, "Out of bounds track index must return null");
    require(re4::XwbStreamReader::LoadTrack(999) == nullptr, "Out of bounds track index must return null");

    std::cout << "SUCCESS: All " << checks << " XwbStreamReader verification checks PASSED!" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "ERROR during test: " << e.what() << std::endl;
    return 1;
  }
}

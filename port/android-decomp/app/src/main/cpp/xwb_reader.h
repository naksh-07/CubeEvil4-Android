#pragma once
#include "sound_music.h"
#include <memory>
#include <string>

namespace re4 {

/**
 * High-performance direct PCM stream reader for Microsoft XACT WaveBank containers (.xwb),
 * specifically engineered to load the uncompressed 48 kHz stereo RE4 Remake Soundtrack Port
 * (bio4bgm.xwb) directly into CubeEvil4's SoundStream pipeline without transcoding or lossy re-compression.
 */
class XwbStreamReader {
public:
  /**
   * Configures the path to bio4bgm.xwb.
   * Validates WBND header, segment tables, and parses entry metadata.
   * Returns true if the file exists and is a valid PCM wavebank.
   */
  static bool Configure(const std::string &xwbPath);

  /**
   * Sets whether Remake soundtrack streams take precedence over GameCube BGM streams.
   */
  static void SetEnabled(bool enabled);

  /**
   * Returns true if Remake soundtrack mode is active and a valid XWB is loaded.
   */
  static bool IsEnabled();

  /**
   * Returns true if a valid XWB container is currently loaded.
   */
  static bool IsAvailable();

  /**
   * Total number of wave entries available in the container.
   */
  static unsigned TrackCount();

  /**
   * Retrieves the configured file path.
   */
  static const std::string &Path();

  /**
   * Reads the specified track index (0 to TrackCount()-1) and builds an immutable SoundStream.
   * Returns nullptr if trackIndex is out of bounds or reading fails.
   */
  static std::shared_ptr<const SoundStream> LoadTrack(unsigned trackIndex);

  /**
   * Resets and releases any cached file descriptors/metadata.
   */
  static void Reset();
};

} // namespace re4

#include "xwb_reader.h"
#include <cstdint>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

namespace re4 {

namespace {

struct WaveBankSegment {
  std::uint32_t offset{};
  std::uint32_t length{};
};

struct TrackEntry {
  std::uint32_t durationSamples{};
  std::uint32_t formatTag{};
  std::uint32_t channels{};
  std::uint32_t rate{};
  std::uint32_t playOffset{};
  std::uint32_t playLength{};
  std::uint32_t loopStart{};
  std::uint32_t loopLength{};
};

class XwbState {
public:
  std::mutex mutex;
  std::string path;
  bool enabled{false};
  bool available{false};
  std::uint32_t waveBaseOffset{0};
  std::vector<TrackEntry> tracks;

  void reset() {
    path.clear();
    enabled = false;
    available = false;
    waveBaseOffset = 0;
    tracks.clear();
  }
};

XwbState g_state;

std::uint32_t readU32(const std::uint8_t *data) {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) |
         (static_cast<std::uint32_t>(data[3]) << 24);
}

} // namespace

bool XwbStreamReader::Configure(const std::string &xwbPath) {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  g_state.reset();

  if (xwbPath.empty()) {
    return false;
  }

  std::ifstream file(xwbPath, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  // Header is 64 bytes
  std::uint8_t header[64];
  if (!file.read(reinterpret_cast<char *>(header), sizeof(header))) {
    return false;
  }

  // Magic 'WBND' = 0x57424E44 (little endian)
  if (header[0] != 'W' || header[1] != 'B' || header[2] != 'N' || header[3] != 'D') {
    return false;
  }

  // 5 segment headers: BankData(0), EntryMeta(1), SeekTable(2), EntryNames(3), WaveData(4)
  WaveBankSegment segments[5];
  for (std::size_t i = 0; i < 5; ++i) {
    segments[i].offset = readU32(header + 12 + i * 8);
    segments[i].length = readU32(header + 12 + i * 8 + 4);
  }

  // Read Segment 0 (Bank Data)
  if (segments[0].length < 80) {
    return false;
  }
  file.seekg(segments[0].offset);
  std::vector<std::uint8_t> bankData(segments[0].length);
  if (!file.read(reinterpret_cast<char *>(bankData.data()), segments[0].length)) {
    return false;
  }

  const std::uint32_t entryCount = readU32(bankData.data() + 4);
  const std::uint32_t metaElemSize = readU32(bankData.data() + 72);

  if (entryCount == 0 || entryCount > 1024 || metaElemSize < 24) {
    return false;
  }

  // Read Segment 1 (Entry Metadata)
  if (segments[1].length < entryCount * metaElemSize) {
    return false;
  }
  file.seekg(segments[1].offset);
  std::vector<std::uint8_t> metaData(entryCount * metaElemSize);
  if (!file.read(reinterpret_cast<char *>(metaData.data()), metaData.size())) {
    return false;
  }

  g_state.tracks.resize(entryCount);
  for (std::uint32_t i = 0; i < entryCount; ++i) {
    const std::uint8_t *elem = metaData.data() + i * metaElemSize;
    const std::uint32_t flagsDur = readU32(elem + 0);
    const std::uint32_t fmt = readU32(elem + 4);
    const std::uint32_t playOff = readU32(elem + 8);
    const std::uint32_t playLen = readU32(elem + 12);
    const std::uint32_t loopSt = readU32(elem + 16);
    const std::uint32_t loopLn = readU32(elem + 20);

    TrackEntry &t = g_state.tracks[i];
    t.durationSamples = flagsDur & 0x7FFFFFFF;
    t.formatTag = fmt & 0x3;                // 0 = PCM
    t.channels = (fmt >> 2) & 0x7;          // 1 = Mono, 2 = Stereo
    t.rate = (fmt >> 5) & 0x3FFFF;          // Samples per second (e.g. 48000)
    t.playOffset = playOff;
    t.playLength = playLen;
    t.loopStart = loopSt;
    t.loopLength = loopLn;
  }

  g_state.path = xwbPath;
  g_state.waveBaseOffset = segments[4].offset;
  g_state.available = true;
  g_state.enabled = true;

  return true;
}

void XwbStreamReader::SetEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  g_state.enabled = enabled;
}

bool XwbStreamReader::IsEnabled() {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  return g_state.enabled && g_state.available;
}

bool XwbStreamReader::IsAvailable() {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  return g_state.available;
}

unsigned XwbStreamReader::TrackCount() {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  return static_cast<unsigned>(g_state.tracks.size());
}

const std::string &XwbStreamReader::Path() {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  return g_state.path;
}

void XwbStreamReader::Reset() {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  g_state.reset();
}

std::shared_ptr<const SoundStream> XwbStreamReader::LoadTrack(unsigned trackIndex) {
  std::lock_guard<std::mutex> lock(g_state.mutex);

  if (!g_state.available || trackIndex >= g_state.tracks.size()) {
    return nullptr;
  }

  const TrackEntry &t = g_state.tracks[trackIndex];
  if (t.formatTag != 0 || t.playLength == 0) {
    return nullptr;
  }

  const unsigned channels = (t.channels == 0) ? 1 : t.channels;
  const std::size_t bytesPerFrame = channels * 2;
  const std::size_t totalFrames = t.playLength / bytesPerFrame;
  if (totalFrames == 0) {
    return nullptr;
  }

  std::ifstream file(g_state.path, std::ios::binary);
  if (!file.is_open()) {
    return nullptr;
  }

  file.seekg(g_state.waveBaseOffset + t.playOffset);
  std::vector<std::uint8_t> rawPcm(t.playLength);
  if (!file.read(reinterpret_cast<char *>(rawPcm.data()), t.playLength)) {
    return nullptr;
  }

  auto stream = std::make_shared<SoundStream>();
  stream->block = 0;
  stream->number = trackIndex;
  stream->rate = t.rate ? t.rate : 48000;
  stream->volume = 127;
  stream->pan = 64;
  stream->stereo = (channels >= 2);
  stream->looping = true;

  stream->left.resize(totalFrames);
  if (stream->stereo) {
    stream->right.resize(totalFrames);
    for (std::size_t i = 0; i < totalFrames; ++i) {
      const std::size_t p = i * 4;
      const std::int16_t l = static_cast<std::int16_t>(rawPcm[p] | (static_cast<std::uint16_t>(rawPcm[p + 1]) << 8));
      const std::int16_t r = static_cast<std::int16_t>(rawPcm[p + 2] | (static_cast<std::uint16_t>(rawPcm[p + 3]) << 8));
      stream->left[i] = l;
      stream->right[i] = r;
    }
  } else {
    for (std::size_t i = 0; i < totalFrames; ++i) {
      const std::size_t p = i * 2;
      const std::int16_t sample = static_cast<std::int16_t>(rawPcm[p] | (static_cast<std::uint16_t>(rawPcm[p + 1]) << 8));
      stream->left[i] = sample;
    }
    stream->right = stream->left;
  }

  // Handle looping region
  if (t.loopLength > 0 && (t.loopStart + t.loopLength) <= totalFrames) {
    const std::size_t lStart = t.loopStart;
    const std::size_t lEnd = t.loopStart + t.loopLength;
    stream->loopLeft.assign(stream->left.begin() + lStart, stream->left.begin() + lEnd);
    if (stream->stereo) {
      stream->loopRight.assign(stream->right.begin() + lStart, stream->right.begin() + lEnd);
    } else {
      stream->loopRight = stream->loopLeft;
    }
  } else {
    stream->loopLeft = stream->left;
    stream->loopRight = stream->right;
  }

  return stream;
}

} // namespace re4

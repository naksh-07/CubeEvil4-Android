#pragma once
// CubeEvil4 Studio's converter (Mods, 19.11.2026; DevJunior's choice: the UHD skins first, a mod picked with the file
// picker, the result a .zip to import in CubeEvil4): character skins of the PC release (RE4 UHD, Steam 2014:
// em/plXX.udas, little-endian, float vertices, textures in BIO4/imagePackHD) become archives the game reads from the
// GameCube discs (em/plXX.drs, big-endian, GX display lists, textures in the archive). A port of
// port/tools/uhd_skin_convert.py and port/tools/re4lfs.py; the same output for the same input, except the textures'
// scaling (Pillow's bicubic filter, here a close one) and the painting of a missing face from the disc's (the PC
// tool's bake_texture with scipy): here, as there without scipy, the disc's colour image.
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace re4::studio::convert {

using Bytes = std::vector<std::uint8_t>;

// An .lfs file of the PC release (RDLX, XMemCompress LZX chunks) unpacked; throws on a broken one.
Bytes UnpackLfs(const Bytes &data);
bool IsLfs(const Bytes &data);

// The mod's BIO4/imagePackHD/*.pack files (unpacked): textures by the palette's reference (pack id, index).
class Packs {
public:
  // a .pack file's path (its header is read now, its textures when asked)
  bool add(const std::string &path);
  std::size_t size() const { return packs_.size(); }
  struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4
  };
  bool has(std::uint32_t ref) const { return find(ref) != nullptr; }
  // the DDS image `index` of the pack `ref` as RGBA; false when missing or of a kind this does not read
  bool image(std::uint32_t ref, std::uint32_t index, Image &out, std::string *why) const;

private:
  struct Pack {
    std::uint32_t id;
    std::string path;
    std::vector<std::uint32_t> offsets;
    std::uint64_t fileSize;
  };
  const Pack *find(std::uint32_t ref) const;
  std::vector<Pack> packs_;
};

struct Options {
  int tex = 128;        // largest texture side
  int texColour = 0;    // of the colour (CMPR) textures, 0: tex
  int texOther = 0;     // of the bump, specular and alpha ones, 0: tex
  int palette = 2048;   // most skinning matrices per model
  // parts that may lie elsewhere in the PC skeleton than on the disc (same id and parent); allFree: any such
  bool allFree = true;
  std::set<int> bonesFree;
  // the skeletons differ (more or other parts): draw the main model with the disc's skeleton (Separate Ways' Ada)
  bool rebind = true;
};

// The disc's archive (`gcName` its file name: pl00.drs ...) with the PC mod's (unpacked .udas) converted into it.
// `log` gets the PC tool's report, a line per entry; false and `why` when it cannot be done.
bool ConvertSkin(const Bytes &gc, const std::string &gcName, const Bytes &pc, const Packs &packs, const Options &o,
                 Bytes &out, std::string &log, std::string *why);

}  // namespace re4::studio::convert

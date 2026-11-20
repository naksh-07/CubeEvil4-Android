// CubeEvil4 Studio's RE4 Remake mod converter on the host (cpp/studio_remesh.inc):
//   studio_remod_check GC_ARCHIVE MOD_FOLDER OUT [--variant FOLDER] [--tex N] [--head 0|1] [--max-tris N]
// MOD_FOLDER (and the variant laid over it) hold natives/STM/... as the mod's .zip does.
#include "studio_convert.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
using namespace re4::studio::convert;
namespace fs = std::filesystem;
static Bytes slurp(const std::string &p) {
  std::ifstream in(p, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
static void addFolder(const std::string &dir, std::map<std::string, Bytes> &files) {
  for (const auto &e : fs::recursive_directory_iterator(dir)) {
    if (!e.is_regular_file()) continue;
    std::string rel = fs::relative(e.path(), dir).generic_string();
    for (auto &c : rel) c = char(std::tolower(static_cast<unsigned char>(c)));
    const auto at = rel.find("natives/stm/");
    if (at == std::string::npos) continue;
    files[rel.substr(at + 12)] = slurp(e.path().string());
  }
}
int main(int argc, char **argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: studio_remod_check GC_ARCHIVE MOD_FOLDER OUT [--variant FOLDER] [--tex N] [--head 0|1] [--max-tris N]\n");
    return 2;
  }
  Options o;
  std::map<std::string, Bytes> files;
  addFolder(argv[2], files);
  for (int i = 4; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--variant")) addFolder(argv[i + 1], files);
    else if (!std::strcmp(argv[i], "--tex")) o.tex = std::atoi(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--head")) o.keepHead = std::atoi(argv[i + 1]) != 0;
    else if (!std::strcmp(argv[i], "--max-tris")) o.maxSkinTris = std::size_t(std::atoi(argv[i + 1]));
  }
  Bytes out;
  std::string log, why;
  const bool ok = ConvertReMod(slurp(argv[1]), argv[1], files, o, out, log, &why);
  std::fputs(log.c_str(), stdout);
  if (!ok) {
    std::fprintf(stderr, "failed: %s\n", why.c_str());
    return 1;
  }
  std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<const char *>(out.data()), long(out.size()));
  return 0;
}

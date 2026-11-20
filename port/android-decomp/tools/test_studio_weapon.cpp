// CubeEvil4 Studio's RE4 Remake weapon converter on the host (ConvertReWeapon, cpp/studio_convert.cpp):
//   studio_weapon_check WEPxx.drs MOD_FOLDER OUT [--variant FOLDER] [--wp wpNNNN] [--tex N]
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
    std::fprintf(stderr, "usage: studio_weapon_check WEPxx.drs MOD_FOLDER OUT [--variant FOLDER] [--wp wpNNNN] [--tex N]\n");
    return 2;
  }
  Options o;
  std::string wp;
  std::map<std::string, Bytes> files;
  addFolder(argv[2], files);
  for (int i = 4; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--variant")) addFolder(argv[i + 1], files);
    else if (!std::strcmp(argv[i], "--tex")) o.tex = std::atoi(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--wp")) wp = argv[i + 1];
  }
  Bytes out;
  std::string log, why;
  std::string name = argv[1];
  if (auto slash = name.find_last_of("/\\"); slash != std::string::npos) name = name.substr(slash + 1);
  const bool ok = ConvertReWeapon(slurp(argv[1]), name, files, wp, o, out, log, &why);
  std::fputs(log.c_str(), stdout);
  if (!ok) {
    std::fprintf(stderr, "failed: %s\n", why.c_str());
    return 1;
  }
  std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<const char *>(out.data()), long(out.size()));
  return 0;
}

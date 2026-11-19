// CubeEvil4 Studio's skin converter on the host (cpp/studio_convert.cpp), to compare with port/tools/uhd_skin_convert.py:
//   studio_convert_check GC_ARCHIVE PC_ARCHIVE OUT [--packs DIR] [--tex N]
// A .lfs file (the archive or a pack) is unpacked first; packs go to OUT.packs/.
#include "studio_convert.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
using namespace re4::studio::convert;
static Bytes slurp(const std::string &p) {
  std::ifstream in(p, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
int main(int argc, char **argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: studio_convert_check GC PC OUT [--packs DIR] [--tex N]\n");
    return 2;
  }
  Options o;
  Packs packs;
  for (int i = 4; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--tex")) o.tex = std::atoi(argv[i + 1]);
    if (!std::strcmp(argv[i], "--packs")) {
      const std::string dir = argv[i + 1], tmp = std::string(argv[3]) + ".packs";
      mkdir(tmp.c_str(), 0755);
      if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d)) {
          std::string n = e->d_name, low = n;
          for (auto &c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
          if (low.size() > 4 && low.substr(low.size() - 4) == ".lfs" && low.find(".pack") != std::string::npos) {
            const Bytes u = UnpackLfs(slurp(dir + "/" + n));
            const std::string out = tmp + "/" + n.substr(0, n.size() - 4);
            std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char *>(u.data()), std::streamsize(u.size()));
            packs.add(out);
          } else if (low.size() > 5 && low.substr(low.size() - 5) == ".pack") {
            packs.add(dir + "/" + n);
          }
        }
        closedir(d);
      }
    }
  }
  Bytes out;
  std::string log, why;
  const std::string gcPath = argv[1];
  if (!ConvertSkin(slurp(gcPath), gcPath.substr(gcPath.find_last_of('/') + 1), slurp(argv[2]), packs, o, out, log, &why)) {
    std::fprintf(stderr, "%s%s\n", log.c_str(), why.c_str());
    return 1;
  }
  std::fputs(log.c_str(), stdout);
  std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size()));
  return 0;
}

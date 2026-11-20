// CubeEvil4 Studio's PC HUD converter on the host (ConvertCoreHud, cpp/studio_convert.cpp):
//   studio_hud_check etc/core.das etc/core.udas[.lfs] 07000000.pack[.lfs] OUT
#include "studio_convert.h"
#include <cstdio>
#include <fstream>
using namespace re4::studio::convert;
static Bytes slurp(const char *p) { std::ifstream in(p, std::ios::binary); return Bytes((std::istreambuf_iterator<char>(in)), {}); }
int main(int c, char **v) {
  Packs packs; if (!packs.add(v[3])) { fprintf(stderr, "pack?\n"); return 2; }
  Options o; Bytes out; std::string log, why;
  bool ok = ConvertCoreHud(slurp(v[1]), slurp(v[2]), packs, o, out, log, &why);
  fputs(log.c_str(), stdout);
  if (!ok) { fprintf(stderr, "failed: %s\n", why.c_str()); return 1; }
  std::ofstream(v[4], std::ios::binary).write((const char *)out.data(), (long)out.size());
}

// CubeEvil4 Studio's converter of UHD skins (studio_convert.h): port/tools/uhd_skin_convert.py and re4lfs.py in C++.
// The names follow the Python tool's, so the two can be read side by side.
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "third_party/bcdec.h"  // BC1..BC7 blocks (Sergii Kudlai, MIT or Unlicense): the RE4 Remake's textures
#include <cctype>
#include "studio_convert.h"

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <cctype>
#include <stdexcept>
#include <tuple>

namespace re4::studio::convert {
namespace {

// ---- bytes --------------------------------------------------------------------------------------------------------

[[noreturn]] void fail(const std::string &what) { throw std::runtime_error(what); }

std::uint32_t le32(const Bytes &b, std::size_t p) {
  if (p + 4 > b.size()) fail("truncated (offset " + std::to_string(p) + ")");
  return std::uint32_t(b[p]) | std::uint32_t(b[p + 1]) << 8 | std::uint32_t(b[p + 2]) << 16 |
         std::uint32_t(b[p + 3]) << 24;
}
std::uint16_t le16(const Bytes &b, std::size_t p) {
  if (p + 2 > b.size()) fail("truncated (offset " + std::to_string(p) + ")");
  return std::uint16_t(b[p] | b[p + 1] << 8);
}
std::int16_t les16(const Bytes &b, std::size_t p) { return std::int16_t(le16(b, p)); }
float lef32(const Bytes &b, std::size_t p) {
  const std::uint32_t u = le32(b, p);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
std::uint32_t be32(const Bytes &b, std::size_t p) {
  if (p + 4 > b.size()) fail("truncated (offset " + std::to_string(p) + ")");
  return std::uint32_t(b[p]) << 24 | std::uint32_t(b[p + 1]) << 16 | std::uint32_t(b[p + 2]) << 8 | b[p + 3];
}
std::uint16_t be16(const Bytes &b, std::size_t p) {
  if (p + 2 > b.size()) fail("truncated (offset " + std::to_string(p) + ")");
  return std::uint16_t(b[p] << 8 | b[p + 1]);
}
float bef32(const Bytes &b, std::size_t p) {
  const std::uint32_t u = be32(b, p);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
std::uint8_t at8(const Bytes &b, std::size_t p) {
  if (p >= b.size()) fail("truncated (offset " + std::to_string(p) + ")");
  return b[p];
}

void put8(Bytes &o, unsigned v) { o.push_back(std::uint8_t(v)); }
void put16(Bytes &o, unsigned v) { o.push_back(std::uint8_t(v >> 8)), o.push_back(std::uint8_t(v)); }
void put32(Bytes &o, std::uint32_t v) { put16(o, v >> 16), put16(o, v & 0xFFFF); }
void putf(Bytes &o, float f) {
  std::uint32_t u;
  std::memcpy(&u, &f, 4);
  put32(o, u);
}
void set32(Bytes &o, std::size_t p, std::uint32_t v) {
  o[p] = std::uint8_t(v >> 24), o[p + 1] = std::uint8_t(v >> 16), o[p + 2] = std::uint8_t(v >> 8),
  o[p + 3] = std::uint8_t(v);
}
std::size_t align(std::size_t n, std::size_t a = 32) { return (n + a - 1) / a * a; }
void pad(Bytes &o, std::size_t a = 32) { o.resize(align(o.size(), a), 0); }

// Python's int(round(x)): to the nearest, halves to even
long long pyround(double x) {
  const int mode = std::fegetround();
  std::fesetround(FE_TONEAREST);
  const double r = std::nearbyint(x);
  std::fesetround(mode);
  return (long long)r;
}
int quant(double v, double scale, int lo = -32768, int hi = 32767) {
  const long long q = pyround(v * scale);
  return int(std::max<long long>(lo, std::min<long long>(hi, q)));
}

std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char *f, ...) {
  char buf[512];
  va_list a;
  va_start(a, f);
  std::vsnprintf(buf, sizeof(buf), f, a);
  va_end(a);
  return buf;
}

// ---- .lfs: XMemCompress LZX (re4lfs.py) -------------------------------------------------------------------------

struct Huff {
  std::vector<std::int32_t> t;  // (symbol << 8 | length), -1 none; indexed by the next 16 bits
};

Huff table(const std::vector<int> &lengths) {
  Huff h;
  h.t.assign(1 << 16, -1);
  unsigned code = 0;
  for (int length = 1; length <= 16; ++length) {
    for (std::size_t sym = 0; sym < lengths.size(); ++sym) {
      if (lengths[sym] != length) continue;
      const unsigned span = 1u << (16 - length), start = code << (16 - length);
      if (start + span > h.t.size()) fail("LZX: Huffman table overflow");
      for (unsigned k = start; k < start + span; ++k) h.t[k] = std::int32_t(sym << 8 | unsigned(length));
      ++code;
    }
    code <<= 1;
  }
  return h;
}

struct Bits {
  const Bytes &d;
  std::size_t pos, end;
  std::uint32_t buf = 0;
  int left = 0;
  Bits(const Bytes &data, std::size_t p, std::size_t e) : d(data), pos(p), end(e) {}
  void fill() {
    while (left <= 16) {
      const unsigned w = pos + 1 < end ? unsigned(d[pos] | d[pos + 1] << 8) : 0u;
      pos += 2;
      buf |= std::uint32_t(w) << (16 - left);
      left += 16;
    }
  }
  unsigned peek16() {
    if (left < 16) fill();
    return (buf >> 16) & 0xFFFF;
  }
  void drop(int n) {
    buf = n >= 32 ? 0 : buf << n;
    left -= n;
  }
  unsigned read(int n) {
    if (n == 0) return 0;
    if (n > 16) {
      const unsigned hi = read(16);
      return hi << (n - 16) | read(n - 16);
    }
    if (left < n) fill();
    const unsigned v = buf >> (32 - n);
    drop(n);
    return v;
  }
  unsigned decode(const Huff &t) {
    const std::int32_t e = t.t[peek16()];
    if (e < 0) fail("LZX: bad Huffman code");
    drop(e & 0xFF);
    return unsigned(e >> 8);
  }
  void alignWord() {
    const int skip = left % 16 ? left % 16 : 16;
    if (left < skip) {
      pos += 2;
    } else {
      left -= skip;
    }
    pos -= 2 * std::size_t(left / 16);
    buf = 0;
    left = 0;
  }
};

const std::array<int, 51> &Extra() {
  static const std::array<int, 51> e = [] {
    std::array<int, 51> a{};
    // EXTRA = [0, 0, 0, 0] + [min(17, i // 2) for i in range(2, 50)][:46]
    for (int i = 4; i < 50; ++i) a[std::size_t(i)] = std::min(17, (i - 2) / 2);
    return a;
  }();
  return e;
}

class Lzx {
public:
  explicit Lzx(int windowBits = 17)
      : wsize_(1u << windowBits), window_(wsize_), slots_(windowBits == 17 ? 34 : 32),
        mainLengths_(256 + std::size_t(slots_) * 8, 0), lenLengths_(249, 0) {
    for (int i = 0; i <= 50; ++i) base_[std::size_t(i)] = i == 0 ? 0 : base_[std::size_t(i - 1)] + (1u << Extra()[std::size_t(i - 1)]);
  }

  Bytes frame(const Bytes &data, std::size_t pos, std::size_t end, unsigned frameSize) {
    Bits bits(data, pos, end);
    if (!header_) {
      header_ = true;
      if (bits.read(1)) e8_ = bits.read(16) << 16 | bits.read(16);
    }
    const std::uint64_t start = wpos_;
    target_ += frameSize;
    const std::uint64_t wmask = wsize_ - 1;
    while (wpos_ < target_) {
      if (blockLeft_ <= 0) {
        const long long carry = -blockLeft_;
        blockType_ = int(bits.read(3));
        blockLeft_ = (long long)(bits.read(16) << 8 | bits.read(8)) - carry;
        if (blockType_ == 2) {
          std::vector<int> al(8);
          for (auto &x : al) x = int(bits.read(3));
          aligned_ = table(al);
        }
        if (blockType_ == 1 || blockType_ == 2) {
          lengths(bits, mainLengths_, 0, 256);
          lengths(bits, mainLengths_, 256, mainLengths_.size());
          main_ = table(mainLengths_);
          lengths(bits, lenLengths_, 0, 249);
          hasLens_ = std::any_of(lenLengths_.begin(), lenLengths_.end(), [](int l) { return l != 0; });
          if (hasLens_) lens_ = table(lenLengths_);
        } else if (blockType_ == 3) {
          bits.alignWord();
          const std::size_t p = bits.pos;
          r_ = {le32(data, p), le32(data, p + 4), le32(data, p + 8)};
          bits.pos = p + 12;
          rawOdd_ = (blockLeft_ + carry) & 1;
        } else {
          fail("LZX: block type " + std::to_string(blockType_));
        }
      }
      const long long run = std::min<long long>(blockLeft_, (long long)(target_ - wpos_));
      if (blockType_ == 3) {
        const std::size_t p = bits.pos;
        for (long long k = 0; k < run; ++k) window_[(wpos_ + std::uint64_t(k)) & wmask] = at8(data, p + std::size_t(k));
        bits.pos = p + std::size_t(run);
        wpos_ += std::uint64_t(run);
        blockLeft_ -= run;
        if (blockLeft_ == 0 && rawOdd_) bits.pos += 1;
        continue;
      }
      std::uint32_t r0 = r_[0], r1 = r_[1], r2 = r_[2];
      const bool aligned = blockType_ == 2;
      const std::uint64_t stop = wpos_ + std::uint64_t(run);
      const std::uint64_t before = wpos_;
      while (wpos_ < stop) {
        unsigned sym = bits.decode(main_);
        if (sym < 256) {
          window_[wpos_ & wmask] = std::uint8_t(sym);
          ++wpos_;
          continue;
        }
        sym -= 256;
        unsigned length = sym & 7;
        if (length == 7) {
          if (!hasLens_) fail("LZX: length tree missing");
          length += bits.decode(lens_);
        }
        length += 2;
        const unsigned slot = sym >> 3;
        std::uint32_t off;
        if (slot > 2) {
          const int extra = Extra()[slot];
          off = base_[slot] - 2;
          if (aligned && extra >= 3) {
            if (extra > 3) off += bits.read(extra - 3) << 3;
            off += bits.decode(aligned_);
          } else if (extra) {
            off += bits.read(extra);
          }
          r2 = r1, r1 = r0, r0 = off;
        } else if (slot == 0) {
          off = r0;
        } else if (slot == 1) {
          off = r1;
          r1 = r0, r0 = off;
        } else {
          off = r2;
          r2 = r0, r0 = off;
        }
        const long long src = (long long)wpos_ - (long long)off;
        if (src < 0 && wpos_ < wsize_) fail("LZX: match before the start");
        for (unsigned k = 0; k < length; ++k)
          window_[(wpos_ + k) & wmask] = window_[std::uint64_t(src + (long long)k) & wmask];
        wpos_ += length;
      }
      r_ = {r0, r1, r2};
      blockLeft_ -= (long long)(wpos_ - before);
    }
    const std::uint64_t n = wpos_ - start;
    if (n > wsize_) fail("LZX: frame larger than the window");
    Bytes out(n);
    for (std::uint64_t k = 0; k < n; ++k) out[k] = window_[(start + k) & wmask];
    if (e8_ && n > 10) {
      for (std::uint64_t i = 0; i < n - 10;) {
        if (out[i] == 0xE8) {
          const long long cur = (long long)(start + i);
          std::int32_t absv;
          std::memcpy(&absv, &out[i + 1], 4);
          if (-cur <= absv && absv < (long long)e8_) {
            const std::int32_t rel = std::int32_t(absv >= 0 ? absv - cur : absv + (long long)e8_);
            std::memcpy(&out[i + 1], &rel, 4);
          }
          i += 5;
        } else {
          ++i;
        }
      }
    }
    return out;
  }

private:
  void lengths(Bits &bits, std::vector<int> &lens, std::size_t first, std::size_t last) {
    std::vector<int> pre(20);
    for (auto &x : pre) x = int(bits.read(4));
    const Huff pt = table(pre);
    std::size_t x = first;
    while (x < last) {
      const unsigned z = bits.decode(pt);
      if (z == 17) {
        const unsigned y = bits.read(4) + 4;
        for (unsigned k = 0; k < y && x + k < lens.size(); ++k) lens[x + k] = 0;
        x += y;
      } else if (z == 18) {
        const unsigned y = bits.read(5) + 20;
        for (unsigned k = 0; k < y && x + k < lens.size(); ++k) lens[x + k] = 0;
        x += y;
      } else if (z == 19) {
        const unsigned y = bits.read(1) + 4;
        const unsigned z2 = bits.decode(pt);
        if (x >= lens.size()) fail("LZX: lengths past the end");
        const int v = (lens[x] + 17 - int(z2)) % 17;
        for (unsigned k = 0; k < y && x + k < lens.size(); ++k) lens[x + k] = v;
        x += y;
      } else {
        lens[x] = (lens[x] + 17 - int(z)) % 17;
        ++x;
      }
    }
  }

  std::uint64_t wsize_;
  std::vector<std::uint8_t> window_;
  std::uint64_t wpos_ = 0;
  int slots_;
  std::vector<int> mainLengths_, lenLengths_;
  std::array<std::uint32_t, 3> r_{1, 1, 1};
  bool header_ = false;
  std::uint32_t e8_ = 0;
  int blockType_ = 0;
  long long blockLeft_ = 0;
  std::uint64_t target_ = 0;
  long long rawOdd_ = 0;
  Huff main_, lens_, aligned_;
  bool hasLens_ = false;
  std::array<std::uint32_t, 51> base_{};
};

Bytes xmemDecompress(const Bytes &data, std::size_t pos, std::size_t end, std::size_t outSize) {
  Lzx lzx(17);
  Bytes out;
  while (out.size() < outSize && pos < end) {
    const unsigned hi = at8(data, pos);
    unsigned frameSize, block;
    if (hi == 0xFF) {
      frameSize = unsigned(at8(data, pos + 1) << 8 | at8(data, pos + 2));
      block = unsigned(at8(data, pos + 3) << 8 | at8(data, pos + 4));
      pos += 5;
    } else {
      frameSize = 0x8000;
      block = hi << 8 | at8(data, pos + 1);
      pos += 2;
    }
    if (block == 0 || frameSize == 0) break;
    const Bytes f = lzx.frame(data, pos, pos + block, frameSize);
    out.insert(out.end(), f.begin(), f.end());
    pos += block;
  }
  if (out.size() < outSize) fail("LZX: chunk ended after " + std::to_string(out.size()) + " bytes");
  out.resize(outSize);
  return out;
}

}  // namespace


namespace {

// ---- archives (.das/.drs/.udas): part table at 0x20, part 0 a DAT archive ----------------------------------------

struct Archive {
  Bytes data;
  bool big = true;
  struct Part {
    std::uint32_t type, size, x, off;
    std::array<std::uint8_t, 16> tail;
  };
  std::vector<Part> parts;
  Bytes body;
  std::vector<std::uint32_t> offsets;
  std::size_t rel = 0;
  std::vector<std::array<std::uint8_t, 4>> types;

  std::uint32_t u(const Bytes &b, std::size_t p) const { return big ? be32(b, p) : le32(b, p); }

  Archive(Bytes d, bool bigEndian) : data(std::move(d)), big(bigEndian) {
    for (int i = 0; i < 31; ++i) {
      const std::size_t r = 0x20 + std::size_t(i) * 0x20;
      Part p{u(data, r), u(data, r + 4), u(data, r + 8), u(data, r + 12), {}};
      if (p.type == 0xFFFFFFFFu) break;
      std::memcpy(p.tail.data(), &data.at(r + 0x10), 16);
      parts.push_back(p);
    }
    if (parts.empty() || parts[0].type != 0) fail("part 0 is not the data archive");
    if (std::size_t(parts[0].off) + parts[0].size > data.size()) fail("the data archive runs past the file");
    body.assign(data.begin() + parts[0].off, data.begin() + parts[0].off + parts[0].size);
    const std::uint32_t n = u(body, 0);
    if (n > 4096) fail("not an archive");
    for (std::uint32_t i = 0; i < n; ++i) offsets.push_back(u(body, 16 + i * 4));
    const std::uint32_t r = u(body, 4);
    rel = 16 + std::size_t(n) * 8 <= r && r < body.size() && r % 32 == 0 ? r : 0;
    for (std::uint32_t i = 0; i < n; ++i) {
      std::array<std::uint8_t, 4> t{};
      std::memcpy(t.data(), &body.at(16 + std::size_t(n) * 4 + i * 4), 4);
      types.push_back(t);
    }
  }
  std::size_t count() const { return offsets.size(); }
  Bytes entry(std::size_t i) const {
    const std::size_t start = offsets[i];
    const std::size_t stop = i + 1 < offsets.size() ? offsets[i + 1] : (rel ? rel : body.size());
    if (start == 0 || start >= stop || stop > body.size()) return {};
    return Bytes(body.begin() + long(start), body.begin() + long(stop));
  }
  std::string kind(std::size_t i) const {
    std::string k(types[i].begin(), types[i].end());
    while (!k.empty() && k.back() == 0) k.pop_back();
    for (auto &c : k) c = char(std::toupper(static_cast<unsigned char>(c)));
    return k;
  }
};

// The PC archive seen with the disc's entries (Aligned): a mod's archive may hold entries the disc's lacks
struct PcView {
  const Archive *pc = nullptr;
  std::vector<long> map;  // per disc entry: the PC entry, -1 none
  std::vector<std::size_t> dropped;
  std::string kind(std::size_t i) const { return map[i] >= 0 ? pc->kind(std::size_t(map[i])) : ""; }
  Bytes entry(std::size_t i) const { return map[i] >= 0 ? pc->entry(std::size_t(map[i])) : Bytes{}; }
};

// difflib.SequenceMatcher(None, a, b, autojunk=False).get_matching_blocks(), for the kinds
void matching(const std::vector<std::string> &a, const std::vector<std::string> &b, std::size_t alo, std::size_t ahi,
              std::size_t blo, std::size_t bhi, std::vector<std::array<std::size_t, 3>> &out) {
  // find_longest_match: the longest equal run, earliest in a, then in b
  std::size_t besti = alo, bestj = blo, bestsize = 0;
  std::map<std::size_t, std::size_t> j2len;
  for (std::size_t i = alo; i < ahi; ++i) {
    std::map<std::size_t, std::size_t> next;
    for (std::size_t j = blo; j < bhi; ++j) {
      if (a[i] != b[j]) continue;
      const auto it = j > 0 ? j2len.find(j - 1) : j2len.end();
      const std::size_t k = (it != j2len.end() ? it->second : 0) + 1;
      next[j] = k;
      if (k > bestsize) besti = i - k + 1, bestj = j - k + 1, bestsize = k;
    }
    j2len.swap(next);
  }
  if (!bestsize) return;
  if (alo < besti && blo < bestj) matching(a, b, alo, besti, blo, bestj, out);
  out.push_back({besti, bestj, bestsize});
  if (besti + bestsize < ahi && bestj + bestsize < bhi) matching(a, b, besti + bestsize, ahi, bestj + bestsize, bhi, out);
}

PcView align(const Archive &gc, const Archive &pc) {
  PcView v;
  v.pc = &pc;
  const std::size_t n = gc.count();
  std::vector<std::string> gk, pk;
  for (std::size_t i = 0; i < n; ++i) gk.push_back(gc.kind(i));
  for (std::size_t i = 0; i < pc.count(); ++i) pk.push_back(pc.kind(i));
  v.map.assign(n, -1);
  if (pc.count() == n) {
    for (std::size_t i = 0; i < n; ++i) v.map[i] = long(i);
    return v;
  }
  bool sameLayout = pk.size() >= gk.size();
  for (std::size_t i = 0; sameLayout && i < gk.size(); ++i)
    sameLayout = gk[i] == pk[i] || gk[i].empty() || pk[i].empty();
  std::vector<bool> used(pk.size(), false);
  if (sameLayout) {
    for (std::size_t i = 0; i < n; ++i)
      if (!gk[i].empty() && !pk[i].empty()) v.map[i] = long(i), used[i] = true;
  } else {
    std::vector<std::array<std::size_t, 3>> blocks;
    matching(gk, pk, 0, gk.size(), 0, pk.size(), blocks);
    blocks.push_back({gk.size(), pk.size(), 0});
    // the opcodes: "equal" blocks map entry by entry, so does a "replace" of equal lengths
    std::size_t i = 0, j = 0;
    for (const auto &blk : blocks) {
      const std::size_t ai = blk[0], bj = blk[1], size = blk[2];
      if (i < ai && j < bj && ai - i == bj - j)
        for (std::size_t k = 0; k < ai - i; ++k) v.map[i + k] = long(j + k);
      for (std::size_t k = 0; k < size; ++k) v.map[ai + k] = long(bj + k);
      i = ai + size, j = bj + size;
    }
    for (long m : v.map)
      if (m >= 0) used[std::size_t(m)] = true;
  }
  for (std::size_t j = 0; j < pk.size(); ++j)
    if (!used[j] && (!sameLayout || !pk[j].empty())) v.dropped.push_back(j);
  return v;
}

Bytes buildBody(const std::vector<Bytes> &entries, const std::vector<std::array<std::uint8_t, 4>> &types) {
  const std::size_t n = entries.size();
  Bytes out(align(16 + n * 8), 0);
  set32(out, 0, std::uint32_t(n));
  for (std::size_t i = 0; i < n; ++i) {
    std::memcpy(&out[16 + n * 4 + i * 4], types[i].data(), 4);
    set32(out, 16 + i * 4, std::uint32_t(out.size()));
    if (entries[i].empty()) continue;
    out.insert(out.end(), entries[i].begin(), entries[i].end());
    pad(out);
  }
  return out;
}

Bytes buildArchive(const Archive &gc, Bytes body) {
  if (gc.rel && be32(body, 4) == 0) {
    pad(body);
    set32(body, 4, std::uint32_t(body.size()));
    body.insert(body.end(), gc.body.begin() + long(gc.rel), gc.body.end());
  }
  Bytes out(gc.data.begin(), gc.data.begin() + 0x20);
  Bytes table(0x3E0, 0);
  std::size_t pos = 0x400 + align(body.size());
  std::vector<Bytes> rest;
  for (std::size_t i = 0; i < gc.parts.size(); ++i) {
    const auto &p = gc.parts[i];
    std::uint32_t recOff, recSize;
    if (i == 0) {
      recOff = 0x400, recSize = std::uint32_t(body.size());
    } else {
      recOff = std::uint32_t(pos), recSize = p.size;
      rest.emplace_back(gc.data.begin() + p.off, gc.data.begin() + p.off + p.size);
      pos = align(pos + p.size);
    }
    set32(table, i * 0x20, p.type), set32(table, i * 0x20 + 4, recSize), set32(table, i * 0x20 + 8, p.x),
        set32(table, i * 0x20 + 12, recOff);
    std::memcpy(&table[i * 0x20 + 0x10], p.tail.data(), 16);
  }
  set32(table, gc.parts.size() * 0x20, 0xFFFFFFFFu);
  out.insert(out.end(), table.begin(), table.end());
  out.insert(out.end(), body.begin(), body.end());
  pad(out);
  for (const auto &r : rest) {
    out.insert(out.end(), r.begin(), r.end());
    pad(out);
  }
  return out;
}

// ---- models --------------------------------------------------------------------------------------------------------

int prim(unsigned type) {
  switch (type) {
  case 6: return 0x98;  // strip
  case 8: return 0x80;  // quads
  case 5: return 0x90;  // triangles
  default: return -1;
  }
}

using Weight = std::vector<std::pair<int, int>>;  // (bone, rate)

struct Run {
  Bytes raw;  // the part header's bytes 0..0x17
  std::uint32_t npoly;
  std::vector<std::pair<unsigned, unsigned>> runs;  // (type, count)
};

struct Model {  // a PC model (little-endian, the fields of cModelData)
  const Bytes &b;
  std::uint32_t head, x04, x08, clr, tex, wgt, parts, flags, ntex, shape, vtx, nrm, version, blend = 0, flip = 0,
      vidx = 0, nidx = 0;
  unsigned wpal, nparts, nlist, shift, x29, wext, nvtx, nnrm;
  std::vector<std::map<unsigned, std::array<int, 3>>> shapes;

  explicit Model(const Bytes &blob) : b(blob) {
    if (blob.size() < 0x50) fail("not a model (too short)");
    head = le32(b, 0), x04 = le32(b, 4), x08 = le32(b, 8), clr = le32(b, 0xC), tex = le32(b, 0x10), wgt = le32(b, 0x14);
    wpal = b[0x18], nparts = b[0x19], nlist = le16(b, 0x1A);
    parts = le32(b, 0x1C), flags = le32(b, 0x20), ntex = le32(b, 0x24);
    shift = b[0x28], x29 = b[0x29], wext = le16(b, 0x2A);
    shape = le32(b, 0x2C), vtx = le32(b, 0x30), nrm = le32(b, 0x34);
    nvtx = le16(b, 0x38), nnrm = le16(b, 0x3A), version = le32(b, 0x3C);
    if (version == 0x20030818) blend = le32(b, 0x40), flip = le32(b, 0x44), vidx = le32(b, 0x48), nidx = le32(b, 0x4C);
    if (version != 0x20010801 && version != 0x20030817 && version != 0x20030818)
      fail(fmt("not a model (version %#x)", version));
    if (shape) {
      const std::uint32_t n = le32(b, shape);
      for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint32_t ofs = le32(b, shape + 4 + k * 8);
        const std::int32_t cnt = std::int32_t(le32(b, shape + 8 + k * 8));
        std::map<unsigned, std::array<int, 3>> d;
        for (std::int32_t j = 0; j < cnt; ++j) {
          const std::size_t q = shape + 4 + ofs + std::size_t(j) * 8;
          d[le16(b, q)] = {les16(b, q + 2), les16(b, q + 4), les16(b, q + 6)};
        }
        shapes.push_back(std::move(d));
      }
    }
    if (nvtx != nnrm) fail(fmt("vertices %u, normals %u: the PC layout has one of each", nvtx, nnrm));
  }
  std::array<double, 3> f3(std::uint32_t off, unsigned i) const {
    return {lef32(b, off + i * 12), lef32(b, off + i * 12 + 4), lef32(b, off + i * 12 + 8)};
  }
  std::vector<Run> runs() const {
    std::vector<Run> out;
    std::size_t p = parts;
    for (unsigned k = 0; k < nlist; ++k) {
      const std::uint32_t size = le32(b, p + 0x18), npoly = le32(b, p + 0x1C);
      const std::uint32_t cnt = size >= 4 ? le32(b, p + 0x20) : 0;
      Run r;
      r.raw.assign(b.begin() + long(p), b.begin() + long(p + 0x18));
      r.npoly = npoly;
      for (std::uint32_t i = 0; i < cnt; ++i) r.runs.push_back({le16(b, p + 0x24 + i * 4), le16(b, p + 0x26 + i * 4)});
      out.push_back(std::move(r));
      p += 0x20 + size;
    }
    return out;
  }
  std::vector<Weight> weights() const {
    const unsigned n = wext > 0xFF ? wext : wpal;
    std::vector<Weight> out;
    for (unsigned i = 0; i < n; ++i) {
      const std::size_t w = wgt + std::size_t(i) * 8;
      const unsigned k = std::min<unsigned>(at8(b, w + 3), 3);
      Weight e;
      for (unsigned j = 0; j < k; ++j) e.push_back({at8(b, w + j), at8(b, w + 4 + j)});
      out.push_back(e);
    }
    return out;
  }
  // (raw 4 bytes, position) per part
  std::vector<std::pair<std::array<std::uint8_t, 4>, std::array<float, 3>>> bones() const {
    std::vector<std::pair<std::array<std::uint8_t, 4>, std::array<float, 3>>> out;
    for (unsigned i = 0; i < nparts; ++i) {
      const std::size_t r = head + std::size_t(i) * 16;
      std::array<std::uint8_t, 4> raw{at8(b, r), at8(b, r + 1), at8(b, r + 2), at8(b, r + 3)};
      out.push_back({raw, {lef32(b, r + 4), lef32(b, r + 8), lef32(b, r + 12)}});
    }
    return out;
  }
};

struct Merged {
  std::vector<Weight> entries;
  std::vector<int> remap;
  int step = 1;
};

Merged mergeWeights(const std::vector<Weight> &entries, std::size_t cap) {
  for (int step : {1, 2, 5, 10, 15, 20, 25, 34, 50, 100}) {
    std::map<Weight, int> index;
    Merged m;
    m.step = step;
    for (const auto &e : entries) {
      Weight key;
      if (e.empty()) {
        key = {{0, 100}};
      } else {
        std::vector<std::pair<int, int>> infl;
        for (const auto &[bone, w] : e) {
          const long long r = std::max<long long>(0, pyround(double(w) / double(step)) * step);
          if (r > 0) infl.push_back({bone, int(r)});
        }
        if (infl.empty()) {
          // max(e, key=rate): the first of the largest
          auto best = e.begin();
          for (auto it = e.begin(); it != e.end(); ++it)
            if (it->second > best->second) best = it;
          infl = {{best->first, 100}};
        }
        long long total = 0;
        for (const auto &x : infl) total += x.second;
        std::vector<std::pair<int, double>> scaled;
        for (const auto &x : infl) scaled.push_back({x.first, double(x.second) * 100.0 / double(total)});
        std::stable_sort(scaled.begin(), scaled.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
        std::vector<int> rates;
        long long sum = 0;
        for (const auto &x : scaled) rates.push_back(int(pyround(x.second))), sum += rates.back();
        rates[0] += int(100 - sum);
        for (std::size_t k = 0; k < scaled.size(); ++k) key.push_back({scaled[k].first, rates[k]});
        std::sort(key.begin(), key.end());
      }
      auto it = index.find(key);
      if (it == index.end()) {
        it = index.emplace(key, int(m.entries.size())).first;
        m.entries.push_back(key);
      }
      m.remap.push_back(it->second);
    }
    if (m.entries.size() <= cap) return m;
  }
  fail("weights do not fit " + std::to_string(cap) + " entries");
}

// the disc model's parts records (16 bytes each, big-endian as they are)
std::vector<Bytes> gcBoneRecords(const Bytes &gcModel) {
  std::vector<Bytes> out;
  const unsigned n = at8(gcModel, 0x19);
  for (unsigned i = 0; i < n; ++i)
    out.emplace_back(gcModel.begin() + long(0x60 + i * 16), gcModel.begin() + long(0x70 + i * 16));
  return out;
}

// Per PC part: the disc's part it is drawn with (rebind_map); `moved` the PC parts that are not alike on the disc
std::map<int, int> rebindMap(const Bytes &gcModel, const Bytes &pcModel, std::vector<int> *moved) {
  std::map<int, std::pair<int, std::array<float, 3>>> gb;
  for (const auto &r : gcBoneRecords(gcModel)) gb[r[0]] = {r[1], {bef32(r, 4), bef32(r, 8), bef32(r, 12)}};
  std::map<int, std::pair<int, std::array<float, 3>>> pc;
  for (const auto &[raw, xyz] : Model(pcModel).bones()) pc[raw[0]] = {raw[1], xyz};
  std::set<int> same;
  for (const auto &[bone, v] : pc) {
    auto it = gb.find(bone);
    if (it == gb.end() || it->second.first != v.first) continue;
    float d = 0;
    for (int k = 0; k < 3; ++k) d = std::max(d, std::fabs(it->second.second[std::size_t(k)] - v.second[std::size_t(k)]));
    if (d <= 0.5f) same.insert(bone);
  }
  if (same.empty()) fail("the skeletons have no part in common");
  std::map<int, int> out;
  for (const auto &[bone, v] : pc) {
    int a = bone, hops = 0;
    while (!same.count(a) && pc.count(a) && hops < 256) a = pc[a].first, ++hops;
    out[bone] = same.count(a) ? a : *same.begin();
    if (!same.count(bone) && moved) moved->push_back(bone);
  }
  return out;
}

std::string checkSkeleton(const Bytes &gcModel, const Bytes &pcModel, const Options &o, std::string &log) {
  std::vector<std::tuple<int, int, std::array<float, 3>>> gb, pb;
  for (const auto &r : gcBoneRecords(gcModel)) gb.push_back({r[0], r[1], {bef32(r, 4), bef32(r, 8), bef32(r, 12)}});
  for (const auto &[raw, xyz] : Model(pcModel).bones()) pb.push_back({raw[0], raw[1], xyz});
  if (gb.size() != pb.size()) return fmt("parts %zu (disc) and %zu (PC)", gb.size(), pb.size());
  std::vector<int> moved;
  for (std::size_t i = 0; i < gb.size(); ++i) {
    const auto &[ga, gp, gx] = gb[i];
    const auto &[pa, pp, px] = pb[i];
    float d = 0;
    for (int k = 0; k < 3; ++k) d = std::max(d, std::fabs(gx[std::size_t(k)] - px[std::size_t(k)]));
    if (ga != pa || gp != pp || d > 0.5f) {
      if ((o.allFree || o.bonesFree.count(int(i))) && ga == pa && gp == pp) {
        moved.push_back(int(i));
        continue;
      }
      return fmt("part %zu differs (disc %d/%d, PC %d/%d)", i, ga, gp, pa, pp);
    }
  }
  if (!moved.empty()) {
    std::string list;
    for (int m : moved) list += (list.empty() ? "" : ", ") + std::to_string(m);
    log += "the PC skeleton's parts " + list + " lie elsewhere: the model keeps them, the disc's motions drive them\n";
  }
  return "";
}

struct Converted {
  Bytes blob;
  std::string stats;
};

Converted convertModel(const Bytes &blob, std::size_t cap, const Bytes *rebind) {
  const Model m(blob);
  const unsigned nv = m.nvtx;
  std::vector<std::array<double, 3>> pos(nv), nrm(nv);
  std::vector<std::array<double, 2>> uv(nv);
  for (unsigned i = 0; i < nv; ++i) {
    pos[i] = m.f3(m.vtx, i);
    auto n = m.f3(m.nrm, i);
    double l = std::hypot(n[0], n[1], n[2]);
    if (l == 0) l = 1.0;
    nrm[i] = {n[0] / l, n[1] / l, n[2] / l};
    uv[i] = {lef32(blob, m.tex + i * 8), lef32(blob, m.tex + i * 8 + 4)};
  }
  const bool hasClr = (m.flags & 0x80000000u) != 0;
  std::vector<std::array<std::uint8_t, 4>> clr;
  double uvScale;
  int uvLo, uvHi;
  if (hasClr) {
    for (unsigned i = 0; i < nv; ++i)
      clr.push_back({at8(blob, m.clr + i * 4), at8(blob, m.clr + i * 4 + 1), at8(blob, m.clr + i * 4 + 2),
                     at8(blob, m.clr + i * 4 + 3)});
    uvScale = 256.0, uvLo = -32768, uvHi = 32767;
  } else {
    uvScale = 32768.0, uvLo = 0, uvHi = 65535;
  }
  std::vector<unsigned> vw(nv, 0);
  if (m.vidx)
    for (unsigned i = 0; i < nv; ++i) vw[i] = le16(blob, m.vidx + i * 2);
  std::vector<Weight> weights = m.weights();
  if (rebind) {
    const auto bmap = rebindMap(*rebind, blob, nullptr);
    for (auto &e : weights) {
      std::vector<std::pair<int, int>> acc;  // insertion order, as Python's dict
      for (const auto &[bone, w] : e) {
        auto f = bmap.find(bone);
        const int to = f != bmap.end() ? f->second : bone;
        auto it = std::find_if(acc.begin(), acc.end(), [to](const auto &x) { return x.first == to; });
        if (it == acc.end()) acc.push_back({to, w});
        else it->second += w;
      }
      std::stable_sort(acc.begin(), acc.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
      e = acc;
    }
  }
  Merged merged;
  if (!weights.empty()) {
    merged = mergeWeights(weights, cap);
  } else {
    merged.entries = {{{0, 100}}};
    merged.remap = {0};
  }
  for (auto &w : vw) w = w < merged.remap.size() ? unsigned(merged.remap[w]) : 0u;
  const bool s8 = merged.entries.size() <= 0xFF;
  double big = 1.0;
  for (const auto &p : pos)
    for (double c : p) big = std::max(big, std::fabs(c));
  int shift = std::max(0, std::min(15, int(std::floor(std::log2(32767.0 / big)))));
  if (m.shift && big * double(1 << m.shift) <= 32767) shift = int(m.shift);
  const double ps = double(1 << shift);

  // shapes: the deltas in the new shift; a vertex keeps its own position when its deltas differ (the lips)
  const double dscale = double(1 << shift) / double(1 << m.shift);
  std::vector<std::map<unsigned, std::array<int, 3>>> deltas;
  for (const auto &sd : m.shapes) {
    std::map<unsigned, std::array<int, 3>> d;
    for (const auto &[v, c] : sd)
      d[v] = {int(pyround(c[0] * dscale)), int(pyround(c[1] * dscale)), int(pyround(c[2] * dscale))};
    deltas.push_back(std::move(d));
  }
  // positions: (x, y, z, weight) unique, with the shape signature in the key
  std::map<std::vector<int>, unsigned> pIndex;
  std::vector<std::array<int, 4>> P;
  std::vector<unsigned> pref(nv);
  for (unsigned v = 0; v < nv; ++v) {
    std::vector<int> key = {quant(pos[v][0], ps), quant(pos[v][1], ps), quant(pos[v][2], ps), int(vw[v])};
    for (const auto &sd : deltas) {
      auto it = sd.find(v);
      if (it == sd.end()) key.insert(key.end(), {0, 0, 0, 0});
      else key.insert(key.end(), {1, it->second[0], it->second[1], it->second[2]});
    }
    auto it = pIndex.find(key);
    if (it == pIndex.end()) {
      it = pIndex.emplace(key, unsigned(P.size())).first;
      P.push_back({key[0], key[1], key[2], key[3]});
    }
    pref[v] = it->second;
  }
  std::map<std::array<int, 4>, unsigned> nIndex;
  std::vector<std::array<int, 4>> N;
  std::vector<unsigned> nref(nv);
  for (unsigned v = 0; v < nv; ++v) {
    std::array<int, 4> key = s8 ? std::array<int, 4>{quant(nrm[v][0], 64, -128, 127), quant(nrm[v][1], 64, -128, 127),
                                                    quant(nrm[v][2], 64, -128, 127), int(vw[v])}
                                : std::array<int, 4>{quant(nrm[v][0], 16384), quant(nrm[v][1], 16384),
                                                    quant(nrm[v][2], 16384), int(vw[v])};
    auto it = nIndex.find(key);
    if (it == nIndex.end()) {
      it = nIndex.emplace(key, unsigned(N.size())).first;
      N.push_back(key);
    }
    nref[v] = it->second;
  }
  std::map<std::array<int, 2>, unsigned> tIndex;
  std::vector<std::array<int, 2>> T;
  std::vector<unsigned> tref(nv);
  for (unsigned v = 0; v < nv; ++v) {
    std::array<int, 2> key{quant(uv[v][0], uvScale, uvLo, uvHi), quant(uv[v][1], uvScale, uvLo, uvHi)};
    auto it = tIndex.find(key);
    if (it == tIndex.end()) {
      it = tIndex.emplace(key, unsigned(T.size())).first;
      T.push_back(key);
    }
    tref[v] = it->second;
  }
  std::map<std::array<std::uint8_t, 4>, unsigned> cIndex;
  std::vector<std::array<std::uint8_t, 4>> C;
  std::vector<unsigned> cref(nv);
  if (hasClr) {
    for (unsigned v = 0; v < nv; ++v) {
      auto it = cIndex.find(clr[v]);
      if (it == cIndex.end()) {
        it = cIndex.emplace(clr[v], unsigned(C.size())).first;
        C.push_back(clr[v]);
      }
      cref[v] = it->second;
    }
  }
  if (std::max(P.size(), N.size()) > 0xFFFF) fail(fmt("more than 65535 vertices after merging (%zu)", P.size()));

  const std::uint32_t flags = (m.flags & ~0x20000000u) | (s8 ? 0x20000000u : 0u);
  Bytes out(0x60, 0);
  std::map<std::string, std::uint32_t> sec;
  auto put = [&](const std::string &name, const Bytes &data, std::size_t a) {
    pad(out, a);
    sec[name] = std::uint32_t(out.size());
    out.insert(out.end(), data.begin(), data.end());
  };
  Bytes head;
  if (rebind) {
    for (const auto &r : gcBoneRecords(*rebind)) head.insert(head.end(), r.begin(), r.end());
  } else {
    for (const auto &[raw, xyz] : m.bones()) {
      head.insert(head.end(), raw.begin(), raw.end());
      for (float f : xyz) putf(head, f);
    }
  }
  put("head", head, 16);
  Bytes vb;
  for (const auto &p : P) put16(vb, unsigned(p[0] & 0xFFFF)), put16(vb, unsigned(p[1] & 0xFFFF)),
      put16(vb, unsigned(p[2] & 0xFFFF)), put16(vb, unsigned(p[3]));
  put("vtx", vb, 32);
  Bytes nb;
  for (const auto &n : N) {
    if (s8) put8(nb, unsigned(n[0] & 0xFF)), put8(nb, unsigned(n[1] & 0xFF)), put8(nb, unsigned(n[2] & 0xFF)),
        put8(nb, unsigned(n[3]));
    else put16(nb, unsigned(n[0] & 0xFFFF)), put16(nb, unsigned(n[1] & 0xFFFF)), put16(nb, unsigned(n[2] & 0xFFFF)),
        put16(nb, unsigned(n[3]));
  }
  put("nrm", nb, 32);
  if (!C.empty()) {
    Bytes cb;
    for (const auto &c : C) cb.insert(cb.end(), c.begin(), c.end());
    put("clr", cb, 32);
  }
  Bytes tb;
  for (const auto &t : T) put16(tb, unsigned(t[0] & 0xFFFF)), put16(tb, unsigned(t[1] & 0xFFFF));
  put("tex", tb, 32);
  Bytes wb;
  for (const auto &e : merged.entries) {
    std::array<int, 3> ids{0, 0, 0}, rates{0, 0, 0};
    for (std::size_t k = 0; k < e.size() && k < 3; ++k) ids[k] = e[k].first, rates[k] = e[k].second;
    if (merged.entries.size() <= 0xFF) {
      put8(wb, unsigned(ids[0])), put8(wb, unsigned(ids[1])), put8(wb, unsigned(ids[2])), put8(wb, unsigned(e.size()));
    } else {
      put16(wb, unsigned(ids[0])), put16(wb, unsigned(ids[1])), put16(wb, unsigned(ids[2])), put16(wb, unsigned(e.size()));
    }
    put8(wb, unsigned(rates[0])), put8(wb, unsigned(rates[1])), put8(wb, unsigned(rates[2])), put8(wb, 0);
  }
  put("wgt", wb, 32);
  Bytes pb;
  unsigned first = 0;
  std::uint64_t totalPolys = 0;
  for (const auto &r : m.runs()) {
    Bytes stream;
    for (const auto &[type, cnt] : r.runs) {
      const int op = prim(type);
      if (op < 0) fail("unknown primitive type " + std::to_string(type));
      put8(stream, unsigned(op)), put16(stream, cnt);
      for (unsigned v = first; v < first + cnt; ++v) {
        if (v >= nv) fail("a run past the vertices");
        put16(stream, pref[v]), put16(stream, nref[v]);
        if (!C.empty()) put16(stream, cref[v]);
        put16(stream, tref[v]);
      }
      first += cnt;
    }
    pad(stream);
    pb.insert(pb.end(), r.raw.begin(), r.raw.end());
    put32(pb, std::uint32_t(stream.size())), put32(pb, r.npoly);
    pb.insert(pb.end(), stream.begin(), stream.end());
    totalPolys += r.npoly;
  }
  if (first != nv) fail(fmt("the runs use %u of %u vertices", first, nv));
  put("parts", pb, 32);
  if (rebind && be32(*rebind, 0x3C) == 0x20030818 && be32(*rebind, 0x40)) {
    const std::uint32_t at = be32(*rebind, 0x40), n = be32(*rebind, at);
    put("blend", Bytes(rebind->begin() + at, rebind->begin() + at + 4 + n * 8), 4);
  } else if (!rebind && m.blend) {
    const std::uint32_t n = le32(blob, m.blend);
    Bytes bl;
    put32(bl, n);
    for (std::uint32_t k = 0; k < n * 4; ++k) put16(bl, le16(blob, m.blend + 4 + k * 2));
    put("blend", bl, 4);
  }
  std::string shapeNote;
  if (!m.shapes.empty()) {
    std::vector<std::vector<std::pair<unsigned, std::array<int, 3>>>> lists;
    for (const auto &sd : deltas) {
      std::map<unsigned, std::array<int, 3>> seen;
      for (const auto &[v, d] : sd)
        if (v < nv && (d[0] || d[1] || d[2])) seen.emplace(pref[v], d);
      lists.emplace_back(seen.begin(), seen.end());
    }
    Bytes table;
    put32(table, std::uint32_t(lists.size()));
    Bytes data;
    const std::uint32_t ofs = std::uint32_t(8 * lists.size());
    for (const auto &lst : lists) {
      put32(table, ofs + std::uint32_t(data.size())), put32(table, std::uint32_t(lst.size()));
      for (const auto &[v, d] : lst) {
        put16(data, v);
        for (int c : d) put16(data, unsigned(std::max(-32768, std::min(32767, c)) & 0xFFFF));
      }
    }
    table.insert(table.end(), data.begin(), data.end());
    put("shape", table, 4);
    std::size_t bad = 0;
    std::string counts;
    for (std::size_t k = 0; k < deltas.size(); ++k) {
      std::map<unsigned, std::array<int, 3>> got(lists[k].begin(), lists[k].end());
      for (unsigned v = 0; v < nv; ++v) {
        auto a = deltas[k].find(v);
        auto g = got.find(pref[v]);
        const std::array<int, 3> d = a != deltas[k].end() ? a->second : std::array<int, 3>{0, 0, 0};
        const std::array<int, 3> gv = g != got.end() ? g->second : std::array<int, 3>{0, 0, 0};
        bad += d != gv;
      }
      counts += (counts.empty() ? "" : "/") + std::to_string(lists[k].size());
    }
    shapeNote = fmt(", %zu shapes (%s deltas), %zu vertices off", lists.size(), counts.c_str(), bad);
    if (bad) fail(fmt("shape deltas do not survive the merge (%zu vertices)", bad));
  }
  if (rebind) {
    if (be32(*rebind, 0x3C) == 0x20030818 && be32(*rebind, 0x44)) {
      const std::uint32_t at = be32(*rebind, 0x44), n = be32(*rebind, at);
      put("flip", Bytes(rebind->begin() + at, rebind->begin() + at + 4 + n * 2), 4);
    }
  } else if (m.flip) {
    const std::uint32_t n = be32(blob, m.flip);  // the PC keeps it big-endian, as on the disc
    put("flip", Bytes(blob.begin() + m.flip, blob.begin() + m.flip + 4 + n * 2), 4);
  }
  pad(out);
  auto get = [&](const char *k) { auto it = sec.find(k); return it != sec.end() ? it->second : 0u; };
  Bytes hdr;
  put32(hdr, sec["head"]), put32(hdr, m.x04), put32(hdr, m.version == 0x20030818 ? 0x50u : m.x08);
  put32(hdr, get("clr")), put32(hdr, sec["tex"]), put32(hdr, sec["wgt"]);
  put8(hdr, unsigned(merged.entries.size() & 0xFF)), put8(hdr, rebind ? at8(*rebind, 0x19) : m.nparts),
      put16(hdr, m.nlist);
  put32(hdr, sec["parts"]), put32(hdr, flags), put32(hdr, m.ntex);
  put8(hdr, unsigned(shift)), put8(hdr, m.x29), put16(hdr, unsigned(merged.entries.size() & 0xFFFF));
  put32(hdr, get("shape")), put32(hdr, sec["vtx"]), put32(hdr, sec["nrm"]);
  put16(hdr, unsigned(P.size())), put16(hdr, unsigned(N.size())), put32(hdr, m.version);
  if (m.version == 0x20030818) put32(hdr, get("blend")), put32(hdr, get("flip"));
  std::copy(hdr.begin(), hdr.end(), out.begin());
  Converted c;
  c.stats = fmt("%zu vertices (PC %u), %zu normals, %zu texture coordinates, %llu polygons, weights %zu -> %zu "
                "(step %d%%), shift %d, %s normals%s, %zu bytes (PC %zu)",
                P.size(), nv, N.size(), T.size(), (unsigned long long)totalPolys, weights.size(),
                merged.entries.size(), merged.step, shift, s8 ? "s8" : "s16", shapeNote.c_str(), out.size(),
                blob.size());
  c.blob = std::move(out);
  return c;
}

}  // namespace


namespace {

// ---- textures ------------------------------------------------------------------------------------------------------

struct Rgba {
  int w = 0, h = 0;
  std::vector<std::uint8_t> px;  // w * h * 4
};

void rgb565(unsigned c, std::uint8_t *o) {
  o[0] = std::uint8_t(((c >> 11) & 31) * 255 / 31), o[1] = std::uint8_t(((c >> 5) & 63) * 255 / 63),
  o[2] = std::uint8_t((c & 31) * 255 / 31);
}

// Pillow's 565 expansion (bcn decoder: the top bits repeated)
void pil565(unsigned v, std::uint8_t *c) {
  const unsigned r = (v & 0xF800) >> 8, g = (v & 0x07E0) >> 3, b = (v & 0x001F) << 3;
  c[0] = std::uint8_t(r | r >> 5), c[1] = std::uint8_t(g | g >> 6), c[2] = std::uint8_t(b | b >> 5);
}

// A DDS file (DXT1, DXT3, DXT5 or uncompressed RGB(A) with masks) as RGBA, as Pillow reads it
bool decodeDds(const Bytes &d, Rgba &img, std::string *why) {
  if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) {
    if (why) *why = "not a DDS file";
    return false;
  }
  const std::uint32_t h = le32(d, 12), w = le32(d, 16), pfFlags = le32(d, 80), four = le32(d, 84),
                      bits = le32(d, 88);
  if (!w || !h || w > 8192 || h > 8192) {
    if (why) *why = "bad size";
    return false;
  }
  img.w = int(w), img.h = int(h);
  img.px.assign(std::size_t(w) * h * 4, 0);
  const std::size_t base = 128;
  if (pfFlags & 4) {
    const bool dxt1 = four == 0x31545844u, dxt3 = four == 0x33545844u, dxt5 = four == 0x35545844u;
    if (!dxt1 && !dxt3 && !dxt5) {
      if (why) *why = fmt("DDS kind %c%c%c%c", char(four), char(four >> 8), char(four >> 16), char(four >> 24));
      return false;
    }
    const std::size_t blockBytes = dxt1 ? 8 : 16;
    const std::uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    if (base + std::size_t(bw) * bh * blockBytes > d.size()) {
      if (why) *why = "truncated DDS";
      return false;
    }
    for (std::uint32_t by = 0; by < bh; ++by)
      for (std::uint32_t bx = 0; bx < bw; ++bx) {
        const std::size_t b = base + (std::size_t(by) * bw + bx) * blockBytes;
        std::uint8_t alpha[16];
        std::fill(alpha, alpha + 16, 255);
        std::size_t cb = b;
        if (dxt3) {
          for (int k = 0; k < 16; ++k) alpha[k] = std::uint8_t(((d[b + std::size_t(k / 2)] >> (4 * (k % 2))) & 15) * 17);
          cb = b + 8;
        } else if (dxt5) {
          const unsigned a0 = d[b], a1 = d[b + 1];
          std::uint64_t ab = 0;
          for (int k = 0; k < 6; ++k) ab |= std::uint64_t(d[b + 2 + std::size_t(k)]) << (8 * k);
          unsigned pal[8];
          pal[0] = a0, pal[1] = a1;
          if (a0 > a1)
            for (int k = 1; k < 7; ++k) pal[k + 1] = ((7 - unsigned(k)) * a0 + unsigned(k) * a1) / 7;
          else {
            for (int k = 1; k < 5; ++k) pal[k + 1] = ((5 - unsigned(k)) * a0 + unsigned(k) * a1) / 5;
            pal[6] = 0, pal[7] = 255;
          }
          for (int k = 0; k < 16; ++k) alpha[k] = std::uint8_t(pal[(ab >> (3 * k)) & 7]);
          cb = b + 8;
        }
        const unsigned c0 = unsigned(d[cb] | d[cb + 1] << 8), c1 = unsigned(d[cb + 2] | d[cb + 3] << 8);
        const std::uint32_t idx = le32(d, cb + 4);
        std::uint8_t pal[4][4];
        pil565(c0, pal[0]), pil565(c1, pal[1]);
        pal[0][3] = pal[1][3] = 255;
        if (c0 > c1 || !dxt1) {
          for (int k = 0; k < 3; ++k) {
            pal[2][k] = std::uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = std::uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
          }
          pal[2][3] = pal[3][3] = 255;
        } else {
          for (int k = 0; k < 3; ++k) pal[2][k] = std::uint8_t((pal[0][k] + pal[1][k]) / 2), pal[3][k] = 0;
          pal[2][3] = 255, pal[3][3] = 0;
        }
        for (int k = 0; k < 16; ++k) {
          const std::uint32_t x = bx * 4 + std::uint32_t(k % 4), y = by * 4 + std::uint32_t(k / 4);
          if (x >= w || y >= h) continue;
          const auto *c = pal[(idx >> (2 * k)) & 3];
          auto *o = &img.px[(std::size_t(y) * w + x) * 4];
          o[0] = c[0], o[1] = c[1], o[2] = c[2], o[3] = dxt1 ? c[3] : alpha[k];
        }
      }
    return true;
  }
  if (pfFlags & 0x40) {  // RGB with masks
    const std::uint32_t rm = le32(d, 92), gm = le32(d, 96), bm = le32(d, 100), am = (pfFlags & 1) ? le32(d, 104) : 0;
    if (bits != 32 && bits != 24) {
      if (why) *why = fmt("DDS %u-bit colour", bits);
      return false;
    }
    const std::size_t step = bits / 8;
    if (base + std::size_t(w) * h * step > d.size()) {
      if (why) *why = "truncated DDS";
      return false;
    }
    auto chan = [](std::uint32_t v, std::uint32_t mask) -> std::uint8_t {
      if (!mask) return 255;
      int sh = 0;
      while (!((mask >> sh) & 1)) ++sh;
      const std::uint32_t max = mask >> sh;
      return std::uint8_t(((v & mask) >> sh) * 255 / max);
    };
    for (std::uint32_t i = 0; i < w * h; ++i) {
      std::uint32_t v = 0;
      for (std::size_t k = 0; k < step; ++k) v |= std::uint32_t(d[base + i * step + k]) << (8 * k);
      auto *o = &img.px[std::size_t(i) * 4];
      o[0] = chan(v, rm), o[1] = chan(v, gm), o[2] = chan(v, bm), o[3] = am ? chan(v, am) : 255;
    }
    return true;
  }
  if (why) *why = "DDS kind not read here";
  return false;
}

// A TGA image (true colour, 24 or 32 bits, plain or run-length; the PC packs hold some among the DDS) as Pillow reads it
bool decodeTga(const Bytes &d, Rgba &img, std::string *why) {
  if (d.size() < 18) return false;
  const unsigned idLen = d[0], cmap = d[1], type = d[2], w = le16(d, 12), h = le16(d, 14), bpp = d[16], desc = d[17];
  if (cmap || (type != 2 && type != 10) || (bpp != 24 && bpp != 32) || !w || !h || w > 8192 || h > 8192) {
    if (why) *why = "not a DDS or TGA image this reads";
    return false;
  }
  const std::size_t step = bpp / 8, count = std::size_t(w) * h;
  std::size_t p = 18 + idLen;
  std::vector<std::uint8_t> raw(count * 4);
  auto pixel = [&](std::size_t i, std::size_t at) {
    raw[i * 4] = d[at + 2], raw[i * 4 + 1] = d[at + 1], raw[i * 4 + 2] = d[at], raw[i * 4 + 3] = step == 4 ? d[at + 3] : 255;
  };
  if (type == 2) {
    if (p + count * step > d.size()) {
      if (why) *why = "truncated TGA";
      return false;
    }
    for (std::size_t i = 0; i < count; ++i) pixel(i, p + i * step);
  } else {
    std::size_t i = 0;
    while (i < count) {
      if (p >= d.size()) {
        if (why) *why = "truncated TGA";
        return false;
      }
      const unsigned head = d[p++], n = (head & 0x7F) + 1;
      if (head & 0x80) {
        if (p + step > d.size()) return false;
        for (unsigned k = 0; k < n && i < count; ++k) pixel(i++, p);
        p += step;
      } else {
        if (p + n * step > d.size()) return false;
        for (unsigned k = 0; k < n && i < count; ++k) pixel(i++, p + k * step);
        p += n * step;
      }
    }
  }
  img.w = int(w), img.h = int(h);
  img.px.resize(count * 4);
  const bool top = desc & 0x20, right = desc & 0x10;
  for (unsigned y = 0; y < h; ++y)
    for (unsigned x = 0; x < w; ++x) {
      const unsigned sy = top ? y : h - 1 - y, sx = right ? w - 1 - x : x;
      std::memcpy(&img.px[(std::size_t(y) * w + x) * 4], &raw[(std::size_t(sy) * w + sx) * 4], 4);
    }
  return true;
}

// Pillow's resize of an RGBA image (bicubic, a = -0.5, its support widened when shrinking; premultiplied alpha)
double cubic(double x) {
  const double a = -0.5;
  x = std::fabs(x);
  if (x < 1) return ((a + 2) * x - (a + 3)) * x * x + 1;
  if (x < 2) return (((x - 5) * x + 8) * x - 4) * a;
  return 0;
}

std::vector<double> resample1d(const std::vector<double> &src, int inSize, int outSize, int stride, int lines,
                               bool horizontal) {
  // src: horizontal: lines rows of inSize * 4 values; vertical: inSize rows of stride values (stride = lines * 4)
  std::vector<double> out(horizontal ? std::size_t(lines) * outSize * 4 : std::size_t(outSize) * stride);
  const double scale = double(inSize) / outSize, filterscale = std::max(1.0, scale), support = 2.0 * filterscale;
  for (int o = 0; o < outSize; ++o) {
    const double center = (o + 0.5) * scale;
    const int xmin = std::max(0, int(std::floor(center - support + 0.5)));
    const int xmax = std::min(inSize, int(std::floor(center + support + 0.5)));
    std::vector<double> k;
    double ww = 0;
    for (int x = xmin; x < xmax; ++x) {
      const double v = cubic((x - center + 0.5) / filterscale);
      k.push_back(v);
      ww += v;
    }
    if (ww != 0)
      for (auto &v : k) v /= ww;
    if (horizontal) {
      for (int l = 0; l < lines; ++l)
        for (int c = 0; c < 4; ++c) {
          double acc = 0;
          for (int x = xmin; x < xmax; ++x) acc += src[(std::size_t(l) * inSize + std::size_t(x)) * 4 + std::size_t(c)] * k[std::size_t(x - xmin)];
          out[(std::size_t(l) * outSize + std::size_t(o)) * 4 + std::size_t(c)] = acc;
        }
    } else {
      for (int i = 0; i < stride; ++i) {
        double acc = 0;
        for (int x = xmin; x < xmax; ++x) acc += src[std::size_t(x) * std::size_t(stride) + std::size_t(i)] * k[std::size_t(x - xmin)];
        out[std::size_t(o) * std::size_t(stride) + std::size_t(i)] = acc;
      }
    }
  }
  return out;
}

Rgba resize(const Rgba &in, int tw, int th) {
  if (in.w == tw && in.h == th) return in;
  std::vector<double> src(in.px.size());
  for (std::size_t i = 0; i < in.px.size(); i += 4) {
    const double a = in.px[i + 3] / 255.0;
    src[i] = in.px[i] * a, src[i + 1] = in.px[i + 1] * a, src[i + 2] = in.px[i + 2] * a, src[i + 3] = in.px[i + 3];
  }
  const auto hor = resample1d(src, in.w, tw, 0, in.h, true);
  const auto ver = resample1d(hor, in.h, th, tw * 4, tw, false);
  Rgba out;
  out.w = tw, out.h = th;
  out.px.resize(std::size_t(tw) * th * 4);
  auto clip = [](double v) { return std::uint8_t(std::max(0.0, std::min(255.0, std::floor(v + 0.5)))); };
  for (std::size_t i = 0; i < out.px.size(); i += 4) {
    const double a = std::max(0.0, std::min(255.0, ver[i + 3]));
    const std::uint8_t ab = clip(a);
    out.px[i + 3] = ab;
    for (int c = 0; c < 3; ++c) out.px[i + std::size_t(c)] = ab ? clip(ver[i + std::size_t(c)] * 255.0 / std::max(1.0, a)) : 0;
  }
  return out;
}

unsigned c565(const std::uint8_t *c) { return unsigned((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3); }

Bytes encCmpr(const Rgba &img) {
  Bytes out;
  const int w = img.w, h = img.h;
  for (int ty = 0; ty < h; ty += 8)
    for (int tx = 0; tx < w; tx += 8)
      for (auto [sx, sy] : {std::pair{0, 0}, std::pair{4, 0}, std::pair{0, 4}, std::pair{4, 4}}) {
        std::array<const std::uint8_t *, 16> cols;
        for (int y = 0; y < 4; ++y)
          for (int x = 0; x < 4; ++x) cols[std::size_t(y * 4 + x)] = &img.px[(std::size_t(ty + sy + y) * w + std::size_t(tx + sx + x)) * 4];
        bool transparent = false;
        std::vector<const std::uint8_t *> opaque;
        for (auto *c : cols) {
          if (c[3] < 128) transparent = true;
          else opaque.push_back(c);
        }
        if (opaque.empty()) opaque.assign(cols.begin(), cols.end());
        auto lum = [](const std::uint8_t *c) { return c[0] * 3 + c[1] * 6 + c[2]; };
        const std::uint8_t *a = opaque[0], *b = opaque[0];
        for (auto *c : opaque) {  // Python's max / min: the first of the largest / smallest
          if (lum(c) > lum(a)) a = c;
          if (lum(c) < lum(b)) b = c;
        }
        unsigned c0 = c565(a), c1 = c565(b);
        if (transparent) {
          if (c0 > c1) std::swap(c0, c1);
        } else {
          if (c0 < c1) std::swap(c0, c1);
          if (c0 == c1) {
            c1 = c0 ? c0 - 1 : 0;
            if (c0 == 0) c0 = 1;
          }
        }
        std::uint8_t p0[3], p1[3];
        rgb565(c0, p0), rgb565(c1, p1);
        int pal[4][3];
        int n;
        for (int k = 0; k < 3; ++k) pal[0][k] = p0[k], pal[1][k] = p1[k];
        if (c0 > c1) {
          for (int k = 0; k < 3; ++k) pal[2][k] = (2 * p0[k] + p1[k]) / 3, pal[3][k] = (p0[k] + 2 * p1[k]) / 3;
          n = 4;
        } else {
          for (int k = 0; k < 3; ++k) pal[2][k] = (p0[k] + p1[k]) / 2;
          n = 3;
        }
        std::uint32_t bits = 0;
        for (auto *c : cols) {
          int k;
          if (transparent && c[3] < 128) {
            k = 3;
          } else {
            k = 0;
            long best = -1;
            for (int i = 0; i < n; ++i) {
              long dd = 0;
              for (int j = 0; j < 3; ++j) dd += long(c[j] - pal[i][j]) * long(c[j] - pal[i][j]);
              if (best < 0 || dd < best) best = dd, k = i;
            }
          }
          bits = bits << 2 | unsigned(k);
        }
        put16(out, c0), put16(out, c1), put32(out, bits);
      }
  return out;
}

Bytes encI8(const Rgba &img) {
  bool useAlpha = false;
  for (std::size_t i = 3; i < img.px.size(); i += 4) useAlpha = useAlpha || img.px[i] < 255;
  Bytes out;
  for (int by = 0; by < img.h; by += 4)
    for (int bx = 0; bx < img.w; bx += 8)
      for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x) {
          const auto *c = &img.px[(std::size_t(by + y) * img.w + std::size_t(bx + x)) * 4];
          out.push_back(useAlpha ? c[3] : std::uint8_t((c[0] * 3 + c[1] * 6 + c[2]) / 10));
        }
  return out;
}

Bytes encIa8(const Rgba &img) {
  Bytes out;
  for (int by = 0; by < img.h; by += 4)
    for (int bx = 0; bx < img.w; bx += 4)
      for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
          const auto *c = &img.px[(std::size_t(by + y) * img.w + std::size_t(bx + x)) * 4];
          out.push_back(c[3]), out.push_back(std::uint8_t((c[0] * 3 + c[1] * 6 + c[2]) / 10));
        }
  return out;
}

// ENCODERS: the PC format -> the GX format and its encoder
int gxFormat(std::uint32_t pcFormat) {
  switch (pcFormat) {
  case 0x0:
  case 0x1: return 0x1;  // I8
  case 0x3: return 0x3;  // IA8
  default: return 0xE;   // CMPR
  }
}
Bytes encode(int gx, const Rgba &img) { return gx == 0x1 ? encI8(img) : gx == 0x3 ? encIa8(img) : encCmpr(img); }

const std::map<int, int> &GxBits() {
  static const std::map<int, int> m = {{0x0, 4}, {0x1, 8}, {0x2, 8}, {0x3, 16}, {0x4, 16}, {0x5, 16}, {0x6, 32},
                                       {0xE, 4}};
  return m;
}

struct DiscImage {
  unsigned h, w, f;
  Bytes data;
};

std::vector<DiscImage> discTextures(const Bytes &tpl) {
  std::vector<DiscImage> out;
  if (tpl.size() < 12 || be32(tpl, 0) != 0x0020AF30) return out;
  const std::uint32_t n = be32(tpl, 4);
  for (std::uint32_t i = 0; i < n; ++i) {
    const std::uint32_t ho = be32(tpl, 12 + i * 8);
    const unsigned h = be16(tpl, ho), w = be16(tpl, ho + 2), f = be32(tpl, ho + 4), data = be32(tpl, ho + 8);
    auto it = GxBits().find(int(f));
    if (it == GxBits().end()) continue;
    const std::size_t len = std::size_t(w) * h * std::size_t(it->second) / 8;
    if (data + len > tpl.size()) continue;
    out.push_back({h, w, f, Bytes(tpl.begin() + data, tpl.begin() + long(data + len))});
  }
  return out;
}

std::size_t tplFound(const Bytes &blob, const Packs &packs) {
  const std::uint32_t n = le32(blob, 4);
  std::size_t found = 0;
  for (std::uint32_t i = 0; i < n; ++i) {
    const std::uint32_t ho = le32(blob, 12 + i * 8), data = le32(blob, ho + 8);
    if (data + 8 <= blob.size() && packs.has(le32(blob, data))) ++found;
  }
  return found;
}

Bytes convertTpl(const Bytes &blob, const Packs &packs, const Options &o, std::vector<std::string> &stats,
                 std::vector<std::string> &notes, const Bytes &gcTpl, const std::set<int> &colourOnly) {
  const std::uint32_t n = le32(blob, 4);
  const auto disc = discTextures(gcTpl);
  const DiscImage *discColour = nullptr;
  for (const auto &t : disc)
    if (t.f == 0xE && (!discColour || t.h * t.w > discColour->h * discColour->w)) discColour = &t;
  struct Out {
    unsigned h, w, gx;
    Bytes data;
    std::uint32_t ws, wt;
    float bias;
  };
  std::vector<Out> images;
  for (std::uint32_t i = 0; i < n; ++i) {
    const std::uint32_t ho = le32(blob, 12 + i * 8);
    const unsigned h = le16(blob, ho), w = le16(blob, ho + 2);
    const std::uint32_t pcFmt = le32(blob, ho + 4), data = le32(blob, ho + 8), ws = le32(blob, ho + 12),
                        wt = le32(blob, ho + 16);
    const float bias = lef32(blob, ho + 28);
    std::uint32_t ref = 0, idx = 0;
    if (data + 8 <= blob.size()) ref = le32(blob, data), idx = le32(blob, data + 4);
    Packs::Image pi;
    std::string why;
    bool got = packs.size() && packs.image(ref, idx, pi, &why);
    if (!got && !why.empty() && packs.has(ref)) notes.push_back(fmt("  texture %#x/%u: %s", ref, idx, why.c_str()));
    int gx = gxFormat(pcFmt);
    if (gx == 0x1 && colourOnly.count(int(i))) gx = 0xE;
    if (!got && gx == 0xE && discColour) {
      images.push_back({discColour->h, discColour->w, discColour->f, discColour->data, ws, wt, bias});
      stats.push_back(fmt("  texture %u: missing (%#x/%u), the disc's %ux%u", i, ref, idx, discColour->w,
                          discColour->h));
      continue;
    }
    Rgba img;
    bool missing = false;
    if (!got) {
      img.w = img.h = 8;
      img.px.assign(8 * 8 * 4, 0);
      for (std::size_t k = 0; k < img.px.size(); k += 4) {
        const std::uint8_t g = gx == 0xE ? 128 : 255;
        img.px[k] = img.px[k + 1] = img.px[k + 2] = g, img.px[k + 3] = 255;
      }
      missing = true;
    } else {
      img.w = pi.width, img.h = pi.height, img.px = std::move(pi.rgba);
    }
    const int limitSet = gx == 0xE ? o.texColour : o.texOther;
    const int limit = limitSet ? limitSet : o.tex;
    const double scale = std::min(1.0, double(limit) / double(std::max(img.w, img.h)));
    const int tw = std::max(8, 1 << int(pyround(std::log2(std::max(1.0, img.w * scale)))));
    const int th = std::max(8, 1 << int(pyround(std::log2(std::max(1.0, img.h * scale)))));
    const Rgba scaled = resize(img, tw, th);
    images.push_back({unsigned(th), unsigned(tw), unsigned(gx), encode(gx, scaled), ws, wt, bias});
    if (missing)
      stats.push_back(fmt("  texture %u: missing (%#x/%u), %s %ux%u -> %dx%d format %#x", i, ref, idx,
                          gx == 0xE ? "grey" : "white", w, h, tw, th, unsigned(gx)));
    else
      stats.push_back(fmt("  texture %u: %#x/%u %ux%u -> %dx%d format %#x", i, ref, idx, w, h, tw, th, unsigned(gx)));
  }
  const std::size_t head = 12 + std::size_t(n) * 8;
  Bytes out(align(head + std::size_t(n) * 0x24), 0);
  set32(out, 0, 0x0020AF30), set32(out, 4, n), set32(out, 8, 12);
  for (std::size_t i = 0; i < images.size(); ++i) {
    const auto &im = images[i];
    pad(out);
    set32(out, 12 + i * 8, std::uint32_t(head + i * 0x24)), set32(out, 16 + i * 8, 0);
    Bytes hd;
    put16(hd, im.h), put16(hd, im.w), put32(hd, im.gx), put32(hd, std::uint32_t(out.size())), put32(hd, im.ws),
        put32(hd, im.wt), put32(hd, 1), put32(hd, 1), putf(hd, im.bias), put32(hd, 0);
    std::copy(hd.begin(), hd.end(), out.begin() + long(head + i * 0x24));
    out.insert(out.end(), im.data.begin(), im.data.end());
  }
  return out;
}

// ---- the archive ---------------------------------------------------------------------------------------------------

using TplUsers = std::map<int, std::vector<int>>;
const TplUsers &PlayerTplUsers() {
  static const TplUsers m = {{1, {0, 6, 9, 12}}, {3, {2, 4, 5}}};
  return m;
}
const std::map<std::string, TplUsers> &CharacterTplUsers() {
  static const std::map<std::string, TplUsers> m = {
      {"pl01", {{1, {0, 4, 6, 13, 16, 17}}, {5, {3}}, {7, {2}}}},
      {"pl05", {{1, {0, 4, 6, 13, 16, 17}}, {5, {3}}, {7, {2}}}},
      {"pl11", {{1, {0, 5, 6, 8, 9, 10, 11, 12}}, {4, {2}}, {7, {3}}}},
      {"pl15", {{1, {0, 5, 6, 8, 9, 10, 11, 12}}, {4, {2}}, {7, {3}}}},
      {"pl0b", {{1, {0, 13, 14, 15, 16, 17}}, {3, {2, 4, 7}}, {6, {5}}, {9, {8}}, {103, {102}}, {119, {118}}, {122, {121}}}},
      {"pl0c", {{1, {0, 13, 14, 15, 16, 17}}, {3, {2, 4, 7}}, {6, {5}}, {9, {8}}, {103, {102}}, {119, {118}}, {122, {121}}}},
  };
  return m;
}

std::set<int> colourSlots(const PcView &pc, std::size_t count, int t, const TplUsers &users, bool player) {
  std::vector<int> list;
  if (count >= 100 || !player) {
    auto it = users.find(t);
    if (it != users.end()) list = it->second;
  } else {
    list = {t - 1};
  }
  std::set<int> colour, other;
  for (int k : list) {
    if (k < 0 || std::size_t(k) >= count || pc.kind(std::size_t(k)) != "BIN") continue;
    const Bytes e = pc.entry(std::size_t(k));
    if (e.empty()) continue;
    try {
      const Model m(e);
      for (const auto &r : m.runs()) {
        colour.insert(r.raw[0xC]);
        for (int x : {int(r.raw[0xD]), int(r.raw[0xE])})
          if (x != 0xFF) other.insert(x);
        if (r.raw[0xB] & 0x10) other.insert(r.raw[0x17]);
      }
    } catch (const std::exception &) {
    }
  }
  std::set<int> out;
  for (int c : colour)
    if (!other.count(c)) out.insert(c);
  return out;
}

Bytes readFile(const std::string &path, std::uint64_t at, std::uint64_t len) {
  std::ifstream in(path, std::ios::binary);
  if (!in) fail("cannot read " + path);
  in.seekg(std::streamoff(at));
  Bytes b(len);
  in.read(reinterpret_cast<char *>(b.data()), std::streamsize(len));
  b.resize(std::size_t(in.gcount()));
  return b;
}

#include "studio_dff.inc"
#include "studio_remesh.inc"

}  // namespace

bool Packs::add(const std::string &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return false;
  const auto size = std::uint64_t(in.tellg());
  if (size < 8) return false;
  in.seekg(0);
  std::uint8_t h[8];
  in.read(reinterpret_cast<char *>(h), 8);
  const std::uint32_t id = std::uint32_t(h[0] | h[1] << 8 | h[2] << 16 | std::uint32_t(h[3]) << 24);
  const std::uint32_t n = std::uint32_t(h[4] | h[5] << 8 | h[6] << 16 | std::uint32_t(h[7]) << 24);
  if (n > 65536 || 8 + std::uint64_t(n) * 4 > size) return false;
  std::vector<std::uint8_t> raw(std::size_t(n) * 4);
  in.read(reinterpret_cast<char *>(raw.data()), std::streamsize(raw.size()));
  Pack p{id, path, {}, size};
  for (std::uint32_t i = 0; i < n; ++i)
    p.offsets.push_back(std::uint32_t(raw[i * 4] | raw[i * 4 + 1] << 8 | raw[i * 4 + 2] << 16 |
                                      std::uint32_t(raw[i * 4 + 3]) << 24));
  // a later pack of the same id replaces the earlier (Python's dict)
  for (auto &q : packs_)
    if (q.id == id) {
      q = std::move(p);
      return true;
    }
  packs_.push_back(std::move(p));
  return true;
}

const Packs::Pack *Packs::find(std::uint32_t ref) const {
  for (const auto &p : packs_)
    if (p.id == ref) return &p;
  const Pack *only = nullptr;
  int same = 0;
  for (const auto &p : packs_)
    if (p.id >> 24 == ref >> 24) only = &p, ++same;
  return same == 1 ? only : nullptr;
}

bool Packs::image(std::uint32_t ref, std::uint32_t index, Image &out, std::string *why) const {
  const Pack *p = find(ref);
  if (!p || index >= p->offsets.size()) return false;
  const std::uint64_t start = std::uint64_t(p->offsets[index]) + 16;
  std::uint64_t stop = p->fileSize;
  for (std::uint32_t o : p->offsets)
    if (o > p->offsets[index] && o < stop) stop = o;
  if (start >= stop) return false;
  try {
    const Bytes dds = readFile(p->path, start, stop - start);
    Rgba img;
    const bool dds_ = dds.size() >= 4 && std::memcmp(dds.data(), "DDS ", 4) == 0;
    if (!(dds_ ? decodeDds(dds, img, why) : decodeTga(dds, img, why))) return false;
    out.width = img.w, out.height = img.h, out.rgba = std::move(img.px);
    return true;
  } catch (const std::exception &e) {
    if (why) *why = e.what();
    return false;
  }
}

bool ConvertSkin(const Bytes &gcData, const std::string &gcName, const Bytes &pcData, const Packs &packs,
                 const Options &o, Bytes &result, std::string &log, std::string *why) {
  try {
    const Archive gc(gcData, true);
    const Bytes pcBytes = IsLfs(pcData) ? UnpackLfs(pcData) : pcData;
    const Archive pc(pcBytes, false);
    std::string stem = gcName;
    if (auto slash = stem.find_last_of("/\\"); slash != std::string::npos) stem = stem.substr(slash + 1);
    stem = stem.substr(0, stem.find('.'));
    for (auto &c : stem) c = char(std::tolower(static_cast<unsigned char>(c)));
    const auto ch = CharacterTplUsers().find(stem);
    const bool player = ch == CharacterTplUsers().end();
    const TplUsers &users = player ? PlayerTplUsers() : ch->second;
    const PcView view = align(gc, pc);
    if (pc.count() != gc.count()) {
      std::string dropped;
      for (auto j : view.dropped) dropped += (dropped.empty() ? "" : ", ") + std::to_string(j);
      log += fmt("the archives differ: %zu entries on the disc, %zu in the PC file; matched by kind, PC entries left "
                 "out: [%s]\n", gc.count(), pc.count(), dropped.c_str());
    }
    std::optional<Bytes> rebind0;
    if (gc.kind(0) == "BIN" && view.kind(0) == "BIN") {
      const std::string differs = checkSkeleton(gc.entry(0), view.entry(0), o, log);
      if (!differs.empty() && o.rebind) {
        std::vector<int> moved;
        rebindMap(gc.entry(0), view.entry(0), &moved);
        rebind0 = gc.entry(0);
        log += fmt("the skeletons differ (%s): drawn with the disc's %u parts; %zu PC parts move with their nearest "
                   "common part\n", differs.c_str(), unsigned(gc.entry(0)[0x19]), moved.size());
      } else if (!differs.empty()) {
        fail("the skeletons differ: " + differs + "; the disc's motions would not fit this model");
      }
    }
    std::vector<Bytes> entries;
    for (std::size_t i = 0; i < gc.count(); ++i) {
      const std::string kindGc = gc.kind(i), kindPc = view.kind(i);
      const Bytes blobPc = view.entry(i);
      if (view.map[i] < 0) {
        entries.push_back(gc.entry(i));
        continue;
      }
      if (kindGc != kindPc) fail(fmt("entry %zu: %s on the disc, %s in the PC file", i, kindGc.c_str(), kindPc.c_str()));
      if (blobPc.empty()) {
        entries.push_back(gc.entry(i));
        continue;
      }
      if (kindPc == "BIN") {
        const Converted c = convertModel(blobPc, std::size_t(o.palette), i == 0 && rebind0 ? &*rebind0 : nullptr);
        log += fmt("entry %3zu BIN: %s\n", i, c.stats.c_str());
        entries.push_back(c.blob);
      } else if (kindPc == "TPL" && tplFound(blobPc, packs) == 0 && !gc.entry(i).empty()) {
        entries.push_back(gc.entry(i));
        log += fmt("entry %3zu TPL: none of its textures in the mod's packs, the disc's\n", i);
      } else if (kindPc == "TPL") {
        std::vector<std::string> stats, notes;
        const Bytes t = convertTpl(blobPc, packs, o, stats, notes, gc.entry(i),
                                   colourSlots(view, gc.count(), int(i), users, player));
        log += fmt("entry %3zu TPL: %zu textures, %zu bytes\n", i, stats.size(), t.size());
        for (const auto &st : stats) log += st + "\n";
        for (const auto &st : notes) log += st + "\n";
        entries.push_back(t);
      } else {
        entries.push_back(gc.entry(i));
      }
    }
    const Bytes body = buildBody(entries, gc.types);
    result = buildArchive(gc, body);
    log += fmt("%s: %zu bytes, data part %zu bytes (disc %zu; the game's place for Leon holds %d)\n", gcName.c_str(),
               result.size(), body.size(), gc.body.size(), 0x118000);
    return true;
  } catch (const std::exception &e) {
    if (why) *why = e.what();
    return false;
  }
}

namespace {

// A skinned mesh (a GTA skin's, an RE4 Remake mod's: its bones named as a GTA ped's, kLeon) with its textures, onto
// Leon's archive `gcData`: the work of ConvertDff below the reading.
void skinOnLeon(const Bytes &gcData, const std::string &gcName, DffMesh m, const std::map<std::string, Rgba> &images,
                std::vector<std::string> notes, const Options &o, Bytes &result, std::string &log, const char *what,
                bool shoulderFit = false) {
    const Archive gc(gcData, true);
    std::string stem = gcName;
    if (auto slash = stem.find_last_of("/\\"); slash != std::string::npos) stem = stem.substr(slash + 1);
    stem = lower(stem.substr(0, stem.find('.')));
    if (stem != "pl00" && stem != "pl08" && stem != "pl09" && stem != "pl10")
      fail(std::string(what) + "s go on Leon for now (pl00, pl08, pl09, pl10), not on " + stem);
    if (gc.count() < 22 || gc.kind(0) != "BIN" || gc.kind(1) != "TPL") fail("not Leon's archive");
    const Bytes body = gc.entry(0);
    const auto skel = discSkeleton(body);
    for (int need : {0, 2, 3, 4, 5, 7, 8, 10, 11, 13, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 28, 40, 55})
      if (!skel.count(need)) fail(fmt("the disc's skeleton has no bone %d", need));

    const std::size_t nb = m.boneAt.size(), nv = m.pos.size();
    log += fmt("%s: %zu vertices, %zu triangles, %zu materials, %zu bones, %zu textures\n", what, nv, m.tris.size(),
               m.matTex.size(), nb, images.size());

    // the bones by name, each to its disc bone
    std::map<std::string, int> byName;
    for (std::size_t k = 0; k < nb; ++k)
      if (!m.boneName[k].empty()) byName[gtaKey(m.boneName[k])] = int(k);
    for (const char *need : {"pelvis", "head", "l foot", "r foot", "l upperarm", "r upperarm"})
      if (!byName.count(need)) fail(std::string("the skin has no bone \"") + need + "\": not a GTA SA ped skeleton");
    auto at = [&](const char *n) { return m.boneAt[std::size_t(byName.at(n))]; };

    // The whole skin into Leon's space (up, forward, its left) at the size of his trunk: hips to head as long as his.
    // Leon's skeleton stays as it is (his motions, his grips on the weapons and the knife are made for it): the trunk
    // (hips, back, neck, head) keeps the skin's own shape and sits on his hips; the limbs are turned at his joints to
    // point as his bones do and stretched to his bones' lengths, so the hands are where his are.
    const V3 feet = (at("l foot") + at("r foot")) * 0.5;
    const bool toes = byName.count("l toe0") && byName.count("r toe0");
    const V3 toeMid = toes ? (at("l toe0") + at("r toe0")) * 0.5 : feet;
    const V3 up = unit(at("head") - feet);
    V3 fwd = toes ? toeMid - feet : V3{};
    if (len(fwd - up * dot(fwd, up)) < 1e-6) fwd = cross(up, at("l upperarm") - at("r upperarm"));
    fwd = unit(fwd - up * dot(fwd, up));
    V3 left = cross(up, fwd);
    bool mirror = false;
    if (dot(at("l upperarm") - at("r upperarm"), left) < 0) left = left * -1.0, mirror = true;
    // GTA skins: hips to head as long as his, on his hips. RE4 Remake mods (Leon himself, his bones not placed as on
    // the disc: the Remake's hip joint is at the thighs, the disc's pelvis bone at the waist): the thighs to the
    // shoulders as his, the thighs' middle on his (their head and feet then within a few cm of his: limbs ~1.0..1.1).
    double gtaTrunk, leonTrunk;
    V3 hips, h;
    if (shoulderFit) {
      const V3 th = (at("l thigh") + at("r thigh")) * 0.5, sh = (at("l upperarm") + at("r upperarm")) * 0.5;
      gtaTrunk = dot(sh - th, up), leonTrunk = (skel.at(13).at.y + skel.at(7).at.y - skel.at(22).at.y - skel.at(18).at.y) * 0.5;
      hips = th, h = (skel.at(22).at + skel.at(18).at) * 0.5;
    } else {
      gtaTrunk = dot(at("head") - at("pelvis"), up), leonTrunk = skel.at(4).at.y - skel.at(17).at.y;
      hips = at("pelvis"), h = skel.at(17).at;
    }
    if (gtaTrunk <= 0) fail("the skin's head (or shoulders) is not above its hips");
    const double scale = leonTrunk / gtaTrunk;
    auto G = [&](V3 v) {  // the skin's hips on Leon's
      const V3 d = v - hips;
      return V3{h.x + scale * dot(d, left), h.y + scale * dot(d, up), h.z + scale * dot(d, fwd)};
    };
    auto Gdir = [&](V3 n) { return unit(V3{dot(n, left), dot(n, up), dot(n, fwd)}); };
    log += fmt("into Leon's space: scale %.1f (his trunk)%s\n", scale, mirror ? ", mirrored (its left side was the other)" : "");

    struct Piece {
      bool set = false;
      int disc = 0;
      M3 r;
      V3 from, to, axis{0, 1, 0};
      double k = 1, c = 1;  // along the bone, across it (half the stretch: a longer limb also a little thicker)
      V3 apply(V3 p) const {
        const V3 q = p - from;
        const double a = dot(q, axis);
        return to + r * (axis * (a * k) + (q - axis * a) * c);
      }
    };
    std::vector<Piece> piece(nb);
    std::vector<V3> gj(nb);
    for (std::size_t b = 0; b < nb; ++b) gj[b] = G(m.boneAt[b]);
    auto isLimb = [](const std::string &k) {
      for (const char *x : {"upperarm", "forearm", "hand", "thigh", "calf", "foot"})
        if (k.find(x) != std::string::npos && k.find("finger") == std::string::npos) return true;
      return false;
    };
    std::vector<std::string> unknown;
    double kMin = 9, kMax = 0;
    std::function<const Piece &(std::size_t, int)> make = [&](std::size_t b, int depth) -> const Piece & {
      Piece &pc = piece[b];
      if (pc.set || depth > 64) return pc;
      const std::string key = gtaKey(m.boneName[b]);
      const BoneMap *bm = nullptr;
      for (const auto &e : kLeon)
        if (key == e.gta) bm = &e;
      if (!bm) unknown.push_back(m.boneName[b]);
      const int parent = m.boneParent[b];
      const bool clavicle = key.find("clavicle") != std::string::npos;
      if (bm && bm->discTo >= 0 && byName.count(bm->gtaTo) && (isLimb(key) || clavicle)) {
        const V3 g = gj[std::size_t(byName.at(bm->gtaTo))] - gj[b];
        // a limb from Leon's joint along his bone; a clavicle from where the trunk puts it to his shoulder joint
        V3 start = skel.at(bm->disc).at;
        if (clavicle && parent >= 0) start = make(std::size_t(parent), depth + 1).apply(gj[b]);
        const V3 l = skel.at(bm->discTo).at - start;
        pc.axis = unit(g);
        pc.r = turn(pc.axis, unit(l));
        pc.k = std::max(0.6, std::min(1.6, len(l) / std::max(1e-6, len(g))));
        pc.c = 1 + (pc.k - 1) * 0.5;
        kMin = std::min(kMin, pc.k), kMax = std::max(kMax, pc.k);
        pc.from = gj[b], pc.to = start;
        pc.disc = bm->disc;
      } else if (parent >= 0) {
        // the trunk follows its root; leaves (head, fingers, toes) and unknown bones move as their parent does
        pc = make(std::size_t(parent), depth + 1);
        pc.set = false;
        if (bm) pc.disc = bm->disc;
      } else {
        // the root of the skeleton: the trunk as it is, on Leon's hips
        pc.from = pc.to = V3{};
        pc.disc = bm ? bm->disc : 0;
      }
      pc.set = true;
      return pc;
    };
    for (std::size_t b = 0; b < nb; ++b) make(b, 0);
    if (!unknown.empty()) {
      std::string list;
      for (const auto &u : unknown) list += (list.empty() ? "" : ", ") + u;
      log += "bones not known here, moved with their parent: " + list + "\n";
    }
    log += fmt("limbs stretched to Leon's bones by %.2f..%.2f\n", kMin, kMax);
    const Bytes &posed = body;

    // the vertices on Leon's skeleton, their weights by his bones
    std::vector<V3> P(nv), N(nv);
    std::vector<Weight> W(nv);
    for (std::size_t v = 0; v < nv; ++v) {
      const V3 g = G(m.pos[v]), gn = Gdir(m.nrm[v]);
      V3 p{}, n{};
      double total = 0;
      std::map<int, double> by;
      for (int j = 0; j < 4; ++j) {
        const double w = m.weight[v][std::size_t(j)];
        const unsigned b = m.bone[v][std::size_t(j)];
        if (w <= 0 || b >= nb) continue;
        p = p + piece[b].apply(g) * w;
        n = n + piece[b].r * gn * w;
        by[piece[b].disc] += w;
        total += w;
      }
      if (total <= 0) {
        P[v] = g, N[v] = gn, W[v] = {{17, 100}};
        continue;
      }
      P[v] = p * (1.0 / total), N[v] = unit(n);
      std::vector<std::pair<int, double>> list(by.begin(), by.end());
      std::stable_sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
      if (list.size() > 3) list.resize(3);
      double sum = 0;
      for (const auto &x : list) sum += x.second;
      Weight e;
      int given = 0;
      for (std::size_t k = 0; k < list.size(); ++k) {
        const int r = k + 1 == list.size() ? 100 - given : int(std::lround(list[k].second * 100.0 / sum));
        if (r <= 0) continue;
        e.push_back({list[k].first, r});
        given += r;
      }
      W[v] = e;
    }

    // Leon's own weights for the skin (DevJunior, 19.11.2026: "the models stretch too much ... the tool has to fix
    // this"): the skin's few bones blend coarsely (a GTA ped has no twist bones, its hips and shoulders bend hard);
    // each vertex takes the weights of the nearest vertices of Leon's own body, head, face and hands (by the inverse
    // square of their distance, only those of the body parts its own bones belong to: an arm keeps arm weights), so
    // it bends as his skin does, with his twist bones. Far from him (a shoulder pad, the helmet) it keeps more of its
    // own.
    if (o.transfer > 0) {
      std::vector<SrcVert> src;
      for (std::size_t e : {0, 2, 4, 6, 14, 17})
        if (e < gc.count() && gc.kind(e) == "BIN" && !gc.entry(e).empty()) collectSkin(gc.entry(e), skel, src);
      std::size_t moved = 0;
      double far = 0;
      for (std::size_t v = 0; v < nv; ++v) {
        // the head keeps its own weights (DevJunior, 19.11.2026: the helmet "deforms" in the cutscenes: near his face
        // it took Leon's jaw, which talks); a vertex mostly on the skin's head, jaw or brows
        {
          double head = 0, all = 0;
          for (int j = 0; j < 4; ++j) {
            const unsigned b = m.bone[v][std::size_t(j)];
            const double w = m.weight[v][std::size_t(j)];
            if (w <= 0 || b >= nb) continue;
            const std::string key = gtaKey(m.boneName[b]);
            all += w;
            if (key == "head" || key == "jaw" || key == "l brow" || key == "r brow") head += w;
          }
          if (all > 0 && head / all > 0.5) continue;
        }
        std::set<int> chains;
        for (const auto &[bone, r] : W[v]) chains.insert(chainOf(bone));
        // the 6 nearest of his vertices on those chains
        std::array<std::pair<double, const SrcVert *>, 6> near;
        near.fill({1e30, nullptr});
        for (const auto &sv : src) {
          if (!chains.count(sv.chain)) continue;
          const V3 d = sv.p - P[v];
          const double dd = dot(d, d);
          if (dd >= near.back().first) continue;
          std::size_t at2 = near.size() - 1;
          while (at2 > 0 && near[at2 - 1].first > dd) near[at2] = near[at2 - 1], --at2;
          near[at2] = {dd, &sv};
        }
        if (!near[0].second) continue;
        std::map<int, double> acc;
        double total = 0;
        for (const auto &[dd, sv] : near) {
          if (!sv) break;
          const double w = 1.0 / (dd + 25.0);
          for (const auto &[bone, r] : sv->w) acc[bone] += w * r;
          total += w * 100.0;
        }
        // near him all his; 250 mm away and more half his, half its own
        const double dist = std::sqrt(near[0].first);
        far = std::max(far, dist);
        const double mine = o.transfer * (dist < 80 ? 1.0 : std::max(0.5, 1.0 - (dist - 80) / 340.0));
        std::map<int, double> mix;
        for (const auto &[bone, a] : acc) mix[bone] += mine * a / total;
        for (const auto &[bone, r] : W[v]) mix[bone] += (1 - mine) * r / 100.0;
        std::vector<std::pair<int, double>> list(mix.begin(), mix.end());
        std::stable_sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        if (list.size() > 3) list.resize(3);
        double sum = 0;
        for (const auto &x : list) sum += x.second;
        Weight e;
        int given = 0;
        for (std::size_t k = 0; k < list.size(); ++k) {
          const int r = k + 1 == list.size() ? 100 - given : int(std::lround(list[k].second * 100.0 / sum));
          if (r <= 0) continue;
          e.push_back({list[k].first, r});
          given += r;
        }
        if (!e.empty()) W[v] = e, ++moved;
      }
      log += fmt("weights from Leon's own skin for %zu of %zu vertices (%zu of his; the farthest %.0f mm off)\n", moved,
                 nv, src.size(), far);
    }

    const std::vector<Weight> Wsoft = W;  // before the plates: the inner layer bends smoothly
    std::vector<char> plated(nv, 0);
    // The armour round the hips (DevJunior, 19.11.2026): a separate small piece of the mesh there (its triangles joined
    // by shared vertices, at most kPlate across) is armour, not cloth. First all its vertices took the piece's average
    // weights, so it moved as one ("the crotch plate looks very bad when running": it stretched between the thighs);
    // made rigid, the hip guards stood off the leg when it lifted, a dark gap under them (second report: "it still
    // breaks"). Now half way: each vertex kStiff of the piece's average and the rest its own smooth weights (half the
    // stretch, no gap; checked in the Studio's view with the knee raised, walking and crouching). A piece hanging
    // between both thighs (each leg at least kBothLegs of its weights) stays one rigid plate, mostly on the pelvis
    // (kPelvis).
    {
      const double kPlate = 540.0;  // the hip guards and thigh plates of a suit of armour are this large
      std::vector<std::size_t> root(nv);
      for (std::size_t v = 0; v < nv; ++v) root[v] = v;
      std::function<std::size_t(std::size_t)> find = [&](std::size_t x) {
        while (root[x] != x) x = root[x] = root[root[x]];
        return x;
      };
      for (const auto &t : m.tris) {
        if (t[0] >= nv || t[1] >= nv || t[2] >= nv) continue;
        root[find(t[1])] = find(t[0]), root[find(t[2])] = find(t[0]);
      }
      std::map<std::size_t, std::vector<std::size_t>> parts2;
      for (std::size_t v = 0; v < nv; ++v) parts2[find(v)].push_back(v);
      std::size_t plates = 0, stiffened = 0, plateVerts = 0;
      for (const auto &[r, vs] : parts2) {
        if (vs.size() < 3) continue;
        V3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
        for (auto v : vs)
          lo = {std::min(lo.x, P[v].x), std::min(lo.y, P[v].y), std::min(lo.z, P[v].z)},
          hi = {std::max(hi.x, P[v].x), std::max(hi.y, P[v].y), std::max(hi.z, P[v].z)};
        if (len(hi - lo) > kPlate) continue;
        // only round the hips (the crotch plate, the hip guards): plates on the shoulders and limbs opened gaps at
        // the joints (DevJunior, 19.11.2026: "the model now shows breaks")
        const V3 mid = (lo + hi) * 0.5, h = skel.at(17).at;
        if (mid.y < h.y - 320 || mid.y > h.y + 120 || std::fabs(mid.x - h.x) > 230) continue;
        double legL = 0, legR = 0, all = 0;
        for (auto v : vs)
          for (const auto &[bone, rate] : W[v]) {
            all += rate;
            if (chainOf(bone) == 3) legL += rate;
            if (chainOf(bone) == 4) legR += rate;
          }
        if (all <= 0) continue;
        const double kBothLegs = 0.15, kPelvis = 0.65, kStiff = 0.5;
        const bool crotch = legL / all >= kBothLegs && legR / all >= kBothLegs;
        const double stiff = crotch ? 1.0 : kStiff;
        std::map<int, double> avg;
        for (auto v : vs)
          for (const auto &[bone, rate] : W[v]) avg[bone] += (crotch ? 1.0 - kPelvis : 1.0) * rate / all;
        if (crotch) avg[17] += kPelvis;
        for (auto v : vs) {
          std::map<int, double> acc;
          for (const auto &[bone, a] : avg) acc[bone] += stiff * a;
          for (const auto &[bone, rate] : W[v]) acc[bone] += (1.0 - stiff) * rate / 100.0;
          std::vector<std::pair<int, double>> list(acc.begin(), acc.end());
          std::stable_sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
          if (list.size() > 3) list.resize(3);
          double sum = 0;
          for (const auto &x : list) sum += x.second;
          Weight e;
          int given = 0;
          for (std::size_t k = 0; k < list.size(); ++k) {
            const int rr = k + 1 == list.size() ? 100 - given : int(std::lround(list[k].second * 100.0 / sum));
            if (rr <= 0) continue;
            e.push_back({list[k].first, rr});
            given += rr;
          }
          W[v] = e;
          plated[v] = crotch;
        }
        ++(crotch ? plates : stiffened), plateVerts += vs.size();
      }
      log += fmt("%zu pieces of the mesh; at the hips %zu crotch plates move rigidly, %zu pieces half stiff (%zu vertices)\n",
                 parts2.size(), plates, stiffened, plateVerts);
    }

    // the triangles by material, facing as the disc's do (corners counter-clockwise round their normal)
    long facing = 0;
    for (const auto &t : m.tris) {
      if (t[0] >= nv || t[1] >= nv || t[2] >= nv) continue;
      const V3 c = cross(P[t[1]] - P[t[0]], P[t[2]] - P[t[0]]);
      facing += dot(c, N[t[0]] + N[t[1]] + N[t[2]]) >= 0 ? 1 : -1;
    }
    std::map<unsigned, std::vector<std::array<unsigned, 3>>> byMat;
    for (const auto &t : m.tris) {
      if (t[0] >= nv || t[1] >= nv || t[2] >= nv || t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue;
      const unsigned mat = t[3] < m.matTex.size() ? t[3] : 0;
      byMat[mat].push_back(facing >= 0 ? std::array<unsigned, 3>{t[0], t[1], t[2]} : std::array<unsigned, 3>{t[0], t[2], t[1]});
    }
    // An inner layer (DevJunior, 19.11.2026: "where it breaks, fill those gaps with the same tone"): the whole skin
    // once more, kInset under its surface (deeper under the rigid plates), with the same texture and the smooth
    // weights: where plates or pieces part at a bending joint, the inside shows the same colours, not the world.
    if (o.innerLayer) {
      const double kInset = 10.0, kInsetPlate = 22.0;
      P.resize(nv * 2), N.resize(nv * 2), W.resize(nv * 2), m.uv.resize(nv * 2);
      for (std::size_t v = 0; v < nv; ++v) {
        P[nv + v] = P[v] - N[v] * (plated[v] ? kInsetPlate : kInset);
        N[nv + v] = N[v], W[nv + v] = Wsoft[v], m.uv[nv + v] = m.uv[v];
      }
      std::size_t inner = 0;
      for (auto &[mat, list] : byMat) {
        const std::size_t n = list.size();
        for (std::size_t k = 0; k < n; ++k) {
          const auto t = list[k];
          list.push_back({unsigned(t[0] + nv), unsigned(t[1] + nv), unsigned(t[2] + nv)});
          ++inner;
        }
      }
      log += fmt("an inner layer of %zu triangles under the surface fills the gaps at the joints\n", inner);
    }
    std::size_t corners = 0;
    for (const auto &[mat, list] : byMat) corners += list.size() * 3;
    if (corners > 0xFFFF) fail(fmt("%zu triangles: at most 21845 fit one model", corners / 3));

    // textures behind Leon's own in the body's TPL; a material without one gets its colour
    const Bytes tpl = gc.entry(1);
    const std::uint32_t n0 = be32(tpl, 4);
    std::vector<std::tuple<unsigned, unsigned, unsigned, Bytes>> add;
    std::map<std::string, unsigned> slotOf;
    std::map<unsigned, unsigned> matSlot;
    for (const auto &[mat, list] : byMat) {
      const std::string name = m.matTex[mat];
      const auto c = m.matColour[mat];
      const std::string key = images.count(name) ? name : fmt("#%02x%02x%02x", c[0], c[1], c[2]);
      auto it = slotOf.find(key);
      if (it == slotOf.end()) {
        Rgba img;
        if (images.count(name)) {
          img = images.at(name);
        } else {
          img.w = img.h = 8;
          img.px.assign(8 * 8 * 4, 255);
          for (std::size_t q = 0; q < img.px.size(); q += 4) img.px[q] = c[0], img.px[q + 1] = c[1], img.px[q + 2] = c[2];
          if (!name.empty()) notes.push_back("  " + name + ": not in the .txd, the material's colour");
        }
        const double sc = std::min(1.0, double(o.tex) / double(std::max(img.w, img.h)));
        const int tw = std::max(8, 1 << int(pyround(std::log2(std::max(1.0, img.w * sc)))));
        const int th = std::max(8, 1 << int(pyround(std::log2(std::max(1.0, img.h * sc)))));
        const Rgba scaled = resize(img, tw, th);
        add.push_back({unsigned(tw), unsigned(th), 0xEu, encCmpr(scaled)});
        it = slotOf.emplace(key, unsigned(n0 + add.size() - 1)).first;
        log += fmt("  texture %u: %s %dx%d -> %dx%d\n", it->second, key.c_str(), img.w, img.h, tw, th);
      }
      matSlot[mat] = it->second;
    }
    for (const auto &nt : notes) log += nt + "\n";
    if (n0 + add.size() > 0xF7) fail("too many textures for one model");

    // the model in the PC release's layout, then the disc's (convertModel, Leon's skeleton kept)
    std::map<Weight, unsigned> wIndex;
    std::vector<Weight> wList;
    Bytes vtx, nrm, tex, clr, vidx;
    for (const auto &[mat, list] : byMat)
      for (const auto &t : list)
        for (unsigned v : t) {
          lefw(vtx, float(P[v].x)), lefw(vtx, float(P[v].y)), lefw(vtx, float(P[v].z));
          lefw(nrm, float(N[v].x)), lefw(nrm, float(N[v].y)), lefw(nrm, float(N[v].z));
          lefw(tex, float(m.uv[v][0])), lefw(tex, float(m.uv[v][1]));
          le32w(clr, 0xFFFFFFFFu);
          auto wi = wIndex.find(W[v]);
          if (wi == wIndex.end()) wi = wIndex.emplace(W[v], unsigned(wList.size())).first, wList.push_back(W[v]);
          le16w(vidx, wi->second);
        }
    Bytes wgt;
    for (const auto &e : wList) {
      for (std::size_t k = 0; k < 3; ++k) wgt.push_back(std::uint8_t(k < e.size() ? e[k].first : 0));
      wgt.push_back(std::uint8_t(e.size()));
      for (std::size_t k = 0; k < 3; ++k) wgt.push_back(std::uint8_t(k < e.size() ? e[k].second : 0));
      wgt.push_back(0);
    }
    Bytes parts;
    const std::uint32_t partsAt0 = be32(body, 0x1C);
    Bytes raw(body.begin() + long(partsAt0), body.begin() + long(partsAt0 + 0x18));
    raw[0xB] = 0, raw[0xD] = raw[0xE] = raw[0xF] = 0xFF, raw[0x17] = 0xFF;
    for (std::size_t k = 0x10; k < 0x17; ++k) raw[k] = 0;
    for (const auto &[mat, list] : byMat) {
      Bytes r = raw;
      r[0xC] = std::uint8_t(matSlot[mat]);
      parts.insert(parts.end(), r.begin(), r.end());
      std::vector<unsigned> runs;
      for (std::size_t left = list.size() * 3; left;) {
        const std::size_t c = std::min<std::size_t>(left, 65535 - 65535 % 3);
        runs.push_back(unsigned(c));
        left -= c;
      }
      le32w(parts, std::uint32_t(4 + runs.size() * 4)), le32w(parts, std::uint32_t(list.size()));
      le32w(parts, std::uint32_t(runs.size()));
      for (unsigned c : runs) le16w(parts, 5), le16w(parts, c);
    }
    Bytes blob(0x50, 0);
    auto section = [&](const Bytes &d) {
      while (blob.size() % 32) blob.push_back(0);
      const std::uint32_t at2 = std::uint32_t(blob.size());
      blob.insert(blob.end(), d.begin(), d.end());
      return at2;
    };
    Bytes head;
    for (const auto &rec : gcBoneRecords(posed)) {
      head.insert(head.end(), rec.begin(), rec.begin() + 4);
      for (int k = 0; k < 3; ++k) lefw(head, bef32(rec, std::size_t(4 + k * 4)));
    }
    const std::uint32_t oHead = section(head), oVtx = section(vtx), oNrm = section(nrm), oTex = section(tex),
                        oClr = section(clr), oWgt = section(wgt), oVidx = section(vidx), oParts = section(parts);
    le32at(blob, 0x00, oHead), le32at(blob, 0x04, be32(body, 4)), le32at(blob, 0x08, be32(body, 8));
    le32at(blob, 0x0C, oClr), le32at(blob, 0x10, oTex), le32at(blob, 0x14, oWgt);
    blob[0x18] = std::uint8_t(wList.size() <= 0xFF ? wList.size() : 0), blob[0x19] = body[0x19];
    le16at(blob, 0x1A, unsigned(byMat.size())), le32at(blob, 0x1C, oParts);
    le32at(blob, 0x20, be32(body, 0x20) | 0x80000000u), le32at(blob, 0x24, std::uint32_t(n0 + add.size()));
    blob[0x28] = 0, blob[0x29] = body[0x29], le16at(blob, 0x2A, unsigned(wList.size() > 0xFF ? wList.size() : 0));
    le32at(blob, 0x2C, 0), le32at(blob, 0x30, oVtx), le32at(blob, 0x34, oNrm);
    le16at(blob, 0x38, unsigned(corners)), le16at(blob, 0x3A, unsigned(corners)), le32at(blob, 0x3C, 0x20030818u);
    le32at(blob, 0x40, 0), le32at(blob, 0x44, 0), le32at(blob, 0x48, oVidx), le32at(blob, 0x4C, 0);
    const Converted conv = convertModel(blob, std::size_t(o.palette), &posed);
    log += "entry   0 BIN: " + conv.stats + "\n";

    // the archive: the new body and TPL, Leon's head, face, hair, outfit, wound and hands folded away
    std::set<std::size_t> fold = {6, 10};
    if (!o.keepHead) fold.insert({2, 4, 5});
    if (stem != "pl00") fold.insert(12);
    for (std::size_t i = 14; i <= 21; ++i) fold.insert(i);
    std::vector<Bytes> entries;
    std::string folded;
    for (std::size_t i = 0; i < gc.count(); ++i) {
      if (i == 0) entries.push_back(conv.blob);
      else if (i == 1) entries.push_back(appendTpl(tpl, add));
      else if (fold.count(i) && gc.kind(i) == "BIN" && !gc.entry(i).empty()) {
        entries.push_back(foldModel(gc.entry(i), skel));
        folded += (folded.empty() ? "" : ", ") + std::to_string(i);
      } else {
        entries.push_back(gc.entry(i));
      }
    }
    log += "Leon's own models folded away: " + folded + (o.keepHead ? " (his head kept)" : "") + "\n";
    const Bytes newBody = buildBody(entries, gc.types);
    result = buildArchive(gc, newBody);
    // a larger player file goes to the mods' place (patch 0901: up to 6 MB)
    log += fmt("%s: %zu bytes, data part %zu bytes (disc %zu; the mods' place for the player holds %d)\n", gcName.c_str(),
               result.size(), newBody.size(), gc.body.size(), 0x600000);
    if (newBody.size() > 0x600000) fail(fmt("too large for the mods' place for the player (%zu bytes): pick smaller "
                                            "textures", newBody.size()));
}

}  // namespace

bool ConvertDff(const Bytes &gcData, const std::string &gcName, const Bytes &dffData, const Bytes &txdData,
                const Options &o, Bytes &result, std::string &log, std::string *why) {
  try {
    DffMesh m = readDff(dffData);
    std::vector<std::string> notes;
    std::map<std::string, Rgba> images;
    if (!txdData.empty()) images = readTxd(txdData, notes);
    skinOnLeon(gcData, gcName, std::move(m), images, std::move(notes), o, result, log, "GTA skin");
    return true;
  } catch (const std::exception &e) {
    if (why) *why = e.what();
    return false;
  }
}

bool ConvertReMod(const Bytes &gcData, const std::string &gcName, const std::map<std::string, Bytes> &files,
                  const Options &o, Bytes &result, std::string &log, std::string *why) {
  try {
    std::vector<std::string> notes;
    std::map<std::string, Rgba> images;
    DffMesh m = readReMod(files, images, notes, log);
    skinOnLeon(gcData, gcName, std::move(m), images, std::move(notes), o, result, log, "RE4 Remake mod", true);
    return true;
  } catch (const std::exception &e) {
    if (why) *why = e.what();
    return false;
  }
}

bool IsLfs(const Bytes &data) { return data.size() >= 4 && std::memcmp(data.data(), "RDLX", 4) == 0; }

Bytes UnpackLfs(const Bytes &data) {
  if (!IsLfs(data)) fail("not an .lfs file");
  const std::uint32_t total = le32(data, 8), count = le32(data, 16);
  Bytes out;
  out.reserve(total);
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint32_t sizes = le32(data, 20 + i * 8), off = le32(data, 24 + i * 8);
    const std::size_t csize = sizes & 0xFFFF, usize = (sizes >> 16) ? (sizes >> 16) : 0x10000;
    const std::size_t start = std::size_t(off) + 0xF + 4;
    if (csize == usize) {
      if (start + usize > data.size()) fail("lfs: truncated");
      out.insert(out.end(), data.begin() + long(start), data.begin() + long(start + usize));
      continue;
    }
    const Bytes c = xmemDecompress(data, start, start + csize, usize);
    out.insert(out.end(), c.begin(), c.end());
  }
  if (out.size() != total) fail("lfs: unpacked " + std::to_string(out.size()) + " bytes, the header says " +
                                std::to_string(total));
  return out;
}

}  // namespace re4::studio::convert

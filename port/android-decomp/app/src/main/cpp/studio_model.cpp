// CubeEvil4 Studio: archives, the model, its textures, the animation's pose and the CPU skinning (studio_model.h)
#include "studio_model.h"
#include "scene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace re4::studio {
namespace {

std::uint32_t be32(const std::vector<std::uint8_t> &b, std::size_t p) {
  if (p > b.size() || b.size() - p < 4) throw std::runtime_error("truncated");
  return std::uint32_t(b[p]) << 24 | std::uint32_t(b[p + 1]) << 16 | std::uint32_t(b[p + 2]) << 8 | b[p + 3];
}

bool readFile(const std::string &path, std::vector<std::uint8_t> &out) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return false;
  const auto n = in.tellg();
  if (n <= 0 || n > 96 * 1024 * 1024) return false;
  out.resize(std::size_t(n));
  in.seekg(0);
  return bool(in.read(reinterpret_cast<char *>(out.data()), n));
}

}  // namespace

bool Archive::open(const std::string &path, std::string *why) {
  path_ = path;
  body_.clear();
  entries_.clear();
  offsets_.clear();
  std::vector<std::uint8_t> file;
  if (!readFile(path, file)) {
    if (why) *why = "cannot be read";
    return false;
  }
  try {
    // a disc archive: the part table (32-byte records from 0x20), part 0 the data archive; else a DAT body
    std::size_t at = 0, len = file.size();
    if (file.size() >= 0x440 && be32(file, 0x20) == 0) {
      const auto l = be32(file, 0x24), o = be32(file, 0x2C);
      if (o >= 0x400 && l >= 16 && o <= file.size() && l <= file.size() - o) at = o, len = l;
    }
    body_.assign(file.begin() + long(at), file.begin() + long(at + len));
    if (body_.size() >= 16 && std::isxdigit(body_[0]) && std::memchr(body_.data(), '\t', 16)) {
      if (why) *why = "packed (Yz2), not read here";
      return false;
    }
    const auto n = be32(body_, 0);
    if (n == 0 || n > 4096 || 16 + std::size_t(n) * 8 > body_.size()) {
      if (why) *why = "no archive";
      return false;
    }
    std::size_t rel = be32(body_, 4);
    if (rel < 16 + std::size_t(n) * 8 || rel >= body_.size() || rel % 32) rel = body_.size();
    for (unsigned i = 0; i < n; ++i) offsets_.push_back(be32(body_, 16 + i * 4));
    for (unsigned i = 0; i < n; ++i) {
      Entry e;
      e.index = i;
      const std::size_t start = offsets_[i], stop = i + 1 < n ? offsets_[i + 1] : rel;
      e.bytes = start && start < stop && stop <= body_.size() ? stop - start : 0;
      char kind[5] = {};
      std::memcpy(kind, body_.data() + 16 + std::size_t(n) * 4 + i * 4, 4);
      for (char &c : kind)
        if (c && (c < 32 || c > 126)) c = '?';
      e.kind = kind;
      while (!e.kind.empty() && (e.kind.back() == ' ' || e.kind.back() == 0)) e.kind.pop_back();
      entries_.push_back(e);
    }
  } catch (const std::exception &e) {
    if (why) *why = e.what();
    return false;
  }
  return true;
}

std::vector<std::uint8_t> Archive::entry(unsigned index) const {
  if (index >= entries_.size() || !entries_[index].bytes) return {};
  const auto start = offsets_[index];
  return {body_.begin() + long(start), body_.begin() + long(start + entries_[index].bytes)};
}

std::vector<unsigned> Archive::of(const char *kind) const {
  std::vector<unsigned> out;
  for (const auto &e : entries_)
    if (e.bytes && e.kind.compare(0, 3, kind) == 0) out.push_back(e.index);
  return out;
}

int Archive::paletteFor(unsigned bin) const {
  for (unsigned i = bin + 1; i < entries_.size(); ++i)
    if (entries_[i].bytes && entries_[i].kind.compare(0, 3, "TPL") == 0) return int(i);
  for (unsigned i = bin; i-- > 0;)
    if (entries_[i].bytes && entries_[i].kind.compare(0, 3, "TPL") == 0) return int(i);
  return -1;
}

Viewer::Viewer() = default;
Viewer::~Viewer() = default;

bool Viewer::buildPiece(const Archive &archive, unsigned bin, int tpl, Piece &piece, std::string *why) {
  try {
    auto bytes = archive.entry(bin);
    if (bytes.empty()) throw std::runtime_error("empty entry");
    piece.model = std::make_unique<SkinnedModel>(bytes);
    const auto &m = piece.model->mesh;
    // the palette's images (a model's material says which: its header byte 12)
    if (tpl < 0) tpl = archive.paletteFor(bin);
    const int textureBase = int(textures_.size());
    unsigned images = 0;
    if (tpl >= 0) {
      const auto palette = archive.entry(unsigned(tpl));
      images = palette.size() >= 8 ? be32(palette, 4) : 0;
      for (unsigned i = 0; i < images && i < 256; ++i) {
        try {
          textures_.push_back(DecodeTpl(palette, i));
        } catch (const std::exception &) {
          textures_.push_back(Texture{});
        }
      }
    }
    for (const auto &primitive : m.primitives) {
      if (primitive.triangles.empty()) continue;
      const auto header = m.materialHeaders.empty() ? std::array<std::uint8_t, 32>{}
                                                    : m.materialHeaders.at(primitive.displayList);
      Draw d;
      d.first = piece.cornerPosition.size() / 3;
      d.count = primitive.triangles.size();
      const unsigned image = header[12];
      d.texture = image < images && !textures_[std::size_t(textureBase) + image].rgba.empty()
                      ? textureBase + int(image) : -1;
      d.alphaTest = (header[11] & 4) != 0;
      for (const auto &triangle : primitive.triangles)
        for (auto corner : triangle) {
          const auto &ref = primitive.vertices.at(corner);
          piece.cornerPosition.push_back(ref.position);
          piece.cornerNormal.push_back(ref.normal);
          const auto uv = m.texcoords.empty() ? std::array<float, 2>{} : m.texcoords.at(ref.texcoord);
          piece.uvs.push_back(uv[0]);
          piece.uvs.push_back(uv[1]);
        }
      piece.draws.push_back(d);
    }
  } catch (const std::exception &e) {
    if (why) *why = e.what();
    return false;
  }
  return true;
}

// the draws and uvs of all pieces in one list (the corners of piece k after those of pieces 0..k-1)
void Viewer::rebuild() {
  draws_.clear();
  uvs_.clear();
  std::size_t base = 0;
  for (const auto &p : pieces_) {
    for (auto d : p.draws) {
      d.first += base;
      draws_.push_back(d);
    }
    uvs_.insert(uvs_.end(), p.uvs.begin(), p.uvs.end());
    base += p.cornerPosition.size() / 3;
  }
}

bool Viewer::loadModel(const Archive &archive, unsigned bin, int tpl, std::string *why) {
  rig_.reset();
  pieces_.clear();
  pieceNames_.clear();
  pieceKeys_.clear();
  model_ = nullptr;
  textures_.clear();
  clip_ = -1;
  Piece piece;
  if (!buildPiece(archive, bin, tpl, piece, why)) return false;
  try {
    parents_.clear();
    for (const auto &j : piece.model->mesh.joints) parents_.push_back(j.parent);
    rig_ = std::make_unique<AnimationRig>(*piece.model);
  } catch (const std::exception &e) {
    if (why) *why = e.what();
    textures_.clear();
    return false;
  }
  model_ = piece.model.get();
  pieces_.push_back(std::move(piece));
  pieceKeys_.push_back({archive.path(), bin});
  pieceNames_.push_back("#" + std::to_string(bin));
  rebuild();
  skin(rig_->bones());
  restMinimum_ = minimum_;
  restMaximum_ = maximum_;
  return true;
}

bool Viewer::togglePiece(const Archive &archive, unsigned bin, int tpl, bool *added, std::string *why) {
  if (!model_) return false;
  for (std::size_t i = 1; i < pieces_.size(); ++i)
    if (pieceKeys_[i].first == archive.path() && pieceKeys_[i].second == bin) {
      pieces_.erase(pieces_.begin() + long(i));
      pieceKeys_.erase(pieceKeys_.begin() + long(i));
      pieceNames_.erase(pieceNames_.begin() + long(i));
      if (added) *added = false;
      rebuild();
      skin(rig_->bones());
      return true;
    }
  const std::size_t texturesBefore = textures_.size();
  Piece piece;
  if (!buildPiece(archive, bin, tpl, piece, why)) {
    textures_.resize(texturesBefore);
    return false;
  }
  try {
    piece.model->validateSkeleton(model_->mesh.joints.size());
  } catch (const std::exception &) {
    if (why) *why = "it belongs to another skeleton";
    textures_.resize(texturesBefore);
    return false;
  }
  pieces_.push_back(std::move(piece));
  pieceKeys_.push_back({archive.path(), bin});
  pieceNames_.push_back("#" + std::to_string(bin));
  if (added) *added = true;
  rebuild();
  skin(rig_->bones());
  return true;
}

int Viewer::addMotions(const Archive &archive, std::string *why) {
  int added = 0;
  const auto fcv = archive.of("FCV");
  std::string base = archive.path();
  if (auto slash = base.find_last_of('/'); slash != std::string::npos) base = base.substr(slash + 1);
  for (unsigned i : fcv) {
    AnimationBytes b;
    b.motion = archive.entry(i);
    // its sequence: the SEQ right after it, if any (the events: steps, sounds)
    if (i + 1 < archive.entries().size() && archive.entries()[i + 1].kind.compare(0, 3, "SEQ") == 0)
      b.sequence = archive.entry(i + 1);
    clips_.push_back(std::move(b));
    names_.push_back(base + " #" + std::to_string(i));
    ++added;
  }
  if (!added && why) *why = "no animations in it";
  return added;
}

void Viewer::clearMotions() {
  clips_.clear();
  names_.clear();
  clip_ = -1;
  if (model_) {
    rig_ = std::make_unique<AnimationRig>(*model_);
    skin(rig_->bones());
  }
}

bool Viewer::setMotion(int clip, std::string *why) {
  if (!model_) return false;
  try {
    if (clip < 0 || clip >= int(clips_.size())) {
      rig_ = std::make_unique<AnimationRig>(*model_);
      clip_ = -1;
      skin(rig_->bones());
      return clip < 0;
    }
    auto rig = std::make_unique<AnimationRig>(*model_, std::vector<AnimationBytes>{clips_[std::size_t(clip)]});
    rig->sample(0, 0);
    rig_ = std::move(rig);
    clip_ = clip;
    skin(rig_->bones());
  } catch (const std::exception &e) {
    if (why) *why = e.what();
    rig_ = std::make_unique<AnimationRig>(*model_);
    clip_ = -1;
    skin(rig_->bones());
    return false;
  }
  return true;
}

float Viewer::motionLength() const {
  return rig_ && clip_ >= 0 ? rig_->maxFrame(0) : 0.f;
}

void Viewer::pose(float frame) {
  if (!rig_) return;
  if (clip_ >= 0) {
    const float last = rig_->maxFrame(0);
    frame = std::isfinite(frame) ? std::clamp(frame, 0.f, last) : 0.f;
    try {
      rig_->sample(0, frame);
    } catch (const std::exception &) {
      return;
    }
  }
  skin(rig_->bones());
}

void Viewer::skin(const std::vector<Matrix34> &input) {
  if (!model_) return;
  std::vector<Matrix34> bones = input;
  if (inPlace_ && !bones.empty() && clip_ >= 0) {
    // the root's travel on the floor taken off every part (the model stays over the grid's centre)
    const float dx = bones[0][3] - rig_->bindCenters()[0][0], dz = bones[0][11] - rig_->bindCenters()[0][2];
    for (auto &b : bones) b[3] -= dx, b[11] -= dz;
  }
  std::size_t corners = 0;
  for (const auto &p : pieces_) corners += p.cornerPosition.size();
  positions_.resize(corners * 3);
  normals_.resize(corners * 3);
  bool first = true;
  std::size_t c = 0;
  for (const auto &piece : pieces_) {
    const auto &model = *piece.model;
    const auto &m = model.mesh;
    std::vector<Matrix34> palette;
    try {
      palette = model.palette(bones, rig_->bindCenters());
    } catch (const std::exception &) {
      c += piece.cornerPosition.size();
      continue;
    }
    std::vector<std::array<float, 3>> pos(m.positions.size()), nor(m.normals.size());
    for (std::size_t i = 0; i < m.positions.size(); ++i)
      pos[i] = TransformPoint(palette[model.positionWeights[i]], m.positions[i]);
    for (std::size_t i = 0; i < m.normals.size(); ++i) {
      const auto &p = palette[model.normalWeights[i]];
      const auto &n = m.normals[i];
      std::array<float, 3> r{p[0] * n[0] + p[1] * n[1] + p[2] * n[2], p[4] * n[0] + p[5] * n[1] + p[6] * n[2],
                             p[8] * n[0] + p[9] * n[1] + p[10] * n[2]};
      const float l = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
      if (l > 1e-6f) r = {r[0] / l, r[1] / l, r[2] / l};
      nor[i] = r;
    }
    for (std::size_t k = 0; k < piece.cornerPosition.size(); ++k, ++c) {
      const auto &p = pos.at(piece.cornerPosition[k]);
      const auto &n = piece.cornerNormal[k] < nor.size() ? nor[piece.cornerNormal[k]] : std::array<float, 3>{0, 1, 0};
      for (unsigned a = 0; a < 3; ++a) {
        positions_[c * 3 + a] = p[a];
        normals_[c * 3 + a] = n[a];
        if (first) minimum_[a] = maximum_[a] = p[a];
        minimum_[a] = std::min(minimum_[a], p[a]);
        maximum_[a] = std::max(maximum_[a], p[a]);
      }
      first = false;
    }
  }
  jointPositions_.resize(bones.size());
  for (std::size_t i = 0; i < bones.size(); ++i) jointPositions_[i] = {bones[i][3], bones[i][7], bones[i][11]};
}

}  // namespace re4::studio

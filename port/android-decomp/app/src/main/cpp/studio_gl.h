#pragma once
// CubeEvil4 Studio's 3D view (GLES 3): the model of studio::Viewer, a floor grid on X/Z with the axes, the skeleton.
#include "studio_model.h"

namespace re4::studio {

struct Camera {
  float yaw = 0.6f, pitch = 0.25f;   // radians around the target
  float distance = 3500.f;           // mm
  std::array<float, 3> target{0, 900, 0};
};

struct ViewOptions {
  bool grid = true, axes = true, skeleton = false, textured = true;
};

class GlView {
public:
  // with a current GLES 3 context (the surface made); false and `why` on a shader error
  bool create(std::string *why);
  void destroy();
  // the textures of a newly loaded model (once per model)
  void upload(const Viewer &viewer);
  void draw(const Viewer &viewer, int width, int height, const Camera &camera, const ViewOptions &options);
  // the matrices of the last draw (for picking): projection * view
  const std::array<float, 16> &viewProjection() const { return viewProjection_; }

private:
  unsigned meshProgram_ = 0, lineProgram_ = 0;
  unsigned meshVao_ = 0, lineVao_ = 0;
  unsigned positionBuffer_ = 0, normalBuffer_ = 0, uvBuffer_ = 0, lineBuffer_ = 0;
  std::vector<unsigned> textures_;
  std::size_t uvCount_ = 0;
  std::array<float, 16> viewProjection_{};
};

// the camera that frames a model's bounds (mm)
Camera FrameBounds(const std::array<float, 3> &minimum, const std::array<float, 3> &maximum);

}  // namespace re4::studio

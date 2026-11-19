// CubeEvil4 Studio: the Java side (StudioBridge / StudioActivity) and the viewer (studio_model.cpp, studio_gl.cpp).
// The UI thread opens archives, loads models and picks animations; the GL thread poses and draws. One lock for both.
#include "studio_gl.h"
#include <android/log.h>
#include <jni.h>
#include <mutex>

namespace {

using namespace re4::studio;

std::mutex gLock;
Archive gArchive;      // the archive of the models list
Viewer gViewer;
GlView gView;
bool gViewReady = false;
bool gUpload = false;  // a new model: its textures and uvs to the GPU
std::string gError;

std::string text(JNIEnv *env, jstring s) {
  if (!s) return {};
  const char *c = env->GetStringUTFChars(s, nullptr);
  std::string out = c ? c : "";
  if (c) env->ReleaseStringUTFChars(s, c);
  return out;
}

jobjectArray strings(JNIEnv *env, const std::vector<std::string> &list) {
  jobjectArray out = env->NewObjectArray(jsize(list.size()), env->FindClass("java/lang/String"), nullptr);
  for (std::size_t i = 0; i < list.size(); ++i) {
    jstring s = env->NewStringUTF(list[i].c_str());
    env->SetObjectArrayElement(out, jsize(i), s);
    env->DeleteLocalRef(s);
  }
  return out;
}

void log(const std::string &what) { __android_log_print(ANDROID_LOG_INFO, "RE4Studio", "%s", what.c_str()); }

}  // namespace

#define STUDIO(name) Java_com_ast_re4decomp_StudioBridge_##name

extern "C" {

// "" or why the archive cannot be shown
JNIEXPORT jstring JNICALL STUDIO(openArchive)(JNIEnv *env, jclass, jstring path) {
  std::lock_guard<std::mutex> lock(gLock);
  std::string why;
  const bool ok = gArchive.open(text(env, path), &why);
  log("archive " + text(env, path) + (ok ? ": " + std::to_string(gArchive.entries().size()) + " entries" : ": " + why));
  return env->NewStringUTF(ok ? "" : why.c_str());
}

// the archive's entries: "index\tkind\tbytes"
JNIEXPORT jobjectArray JNICALL STUDIO(entries)(JNIEnv *env, jclass) {
  std::lock_guard<std::mutex> lock(gLock);
  std::vector<std::string> out;
  for (const auto &e : gArchive.entries())
    out.push_back(std::to_string(e.index) + "\t" + e.kind + "\t" + std::to_string(e.bytes));
  return strings(env, out);
}

JNIEXPORT jstring JNICALL STUDIO(loadModel)(JNIEnv *env, jclass, jint bin, jint tpl) {
  std::lock_guard<std::mutex> lock(gLock);
  std::string why;
  const bool ok = gViewer.loadModel(gArchive, unsigned(bin), tpl, &why);
  if (ok) {
    gViewer.clearMotions();
    gViewer.addMotions(gArchive, nullptr);  // the model's own archive's animations first
    gUpload = true;
  }
  log("model #" + std::to_string(bin) + (ok ? ": " + std::to_string(gViewer.triangles()) + " triangles" : ": " + why));
  return env->NewStringUTF(ok ? "" : why.c_str());
}

// "added", "removed" or why not
JNIEXPORT jstring JNICALL STUDIO(togglePiece)(JNIEnv *env, jclass, jint bin) {
  std::lock_guard<std::mutex> lock(gLock);
  std::string why;
  bool added = false;
  const bool ok = gViewer.togglePiece(gArchive, unsigned(bin), -1, &added, &why);
  if (ok) gUpload = true;
  return env->NewStringUTF(ok ? (added ? "added" : "removed") : why.c_str());
}

// the animations of another archive (a disc archive or a mod pack's .dat) in place of the list: how many, -1 none
JNIEXPORT jint JNICALL STUDIO(useMotions)(JNIEnv *env, jclass, jstring path) {
  std::lock_guard<std::mutex> lock(gLock);
  Archive a;
  std::string why;
  if (!a.open(text(env, path), &why)) {
    gError = why;
    return -1;
  }
  gViewer.clearMotions();
  const int n = gViewer.addMotions(a, &why);
  if (!n) gError = why;
  return n ? n : -1;
}

JNIEXPORT jstring JNICALL STUDIO(lastError)(JNIEnv *env, jclass) {
  std::lock_guard<std::mutex> lock(gLock);
  return env->NewStringUTF(gError.c_str());
}

JNIEXPORT jobjectArray JNICALL STUDIO(motionNames)(JNIEnv *env, jclass) {
  std::lock_guard<std::mutex> lock(gLock);
  std::vector<std::string> out;
  for (int i = 0; i < gViewer.motionCount(); ++i) out.push_back(gViewer.motionName(i));
  return strings(env, out);
}

// "" or why the animation does not fit the model
JNIEXPORT jstring JNICALL STUDIO(setMotion)(JNIEnv *env, jclass, jint clip) {
  std::lock_guard<std::mutex> lock(gLock);
  std::string why;
  const bool ok = gViewer.setMotion(clip, &why);
  return env->NewStringUTF(ok ? "" : (why.empty() ? "it does not fit this skeleton" : why.c_str()));
}

JNIEXPORT jfloat JNICALL STUDIO(motionLength)(JNIEnv *, jclass) {
  std::lock_guard<std::mutex> lock(gLock);
  return gViewer.motionLength();
}

JNIEXPORT void JNICALL STUDIO(setInPlace)(JNIEnv *, jclass, jboolean on) {
  std::lock_guard<std::mutex> lock(gLock);
  gViewer.setInPlace(on);
}

// the camera that frames the model: target x, y, z and the distance
JNIEXPORT jfloatArray JNICALL STUDIO(frameModel)(JNIEnv *env, jclass) {
  std::lock_guard<std::mutex> lock(gLock);
  const Camera c = gViewer.hasModel() ? FrameBounds(gViewer.restMinimum(), gViewer.restMaximum()) : Camera{};
  const float v[4] = {c.target[0], c.target[1], c.target[2], c.distance};
  jfloatArray out = env->NewFloatArray(4);
  env->SetFloatArrayRegion(out, 0, 4, v);
  return out;
}

// "parts · triangles · height"
JNIEXPORT jstring JNICALL STUDIO(info)(JNIEnv *env, jclass) {
  std::lock_guard<std::mutex> lock(gLock);
  if (!gViewer.hasModel()) return env->NewStringUTF("");
  char line[160];
  std::snprintf(line, sizeof(line), "%u bones · %zu triangles · %zu pieces · %.0f mm tall", gViewer.parts(),
                gViewer.triangles(), gViewer.pieces().size(), double(gViewer.restMaximum()[1] - gViewer.restMinimum()[1]));
  return env->NewStringUTF(line);
}

// GL thread: a new surface (a new context: everything on the GPU again)
JNIEXPORT void JNICALL STUDIO(surfaceCreated)(JNIEnv *, jclass) {
  std::lock_guard<std::mutex> lock(gLock);
  std::string why;
  gViewReady = gView.create(&why);
  if (!gViewReady) log("GL: " + why);
  gUpload = true;
}

// GL thread: the pose at `frame` and the picture. flags: 1 grid, 2 axes, 4 skeleton, 8 textures
JNIEXPORT void JNICALL STUDIO(draw)(JNIEnv *, jclass, jint width, jint height, jfloat yaw, jfloat pitch,
                                    jfloat distance, jfloat tx, jfloat ty, jfloat tz, jint flags, jfloat frame) {
  std::lock_guard<std::mutex> lock(gLock);
  if (!gViewReady) return;
  if (gUpload) {
    gView.upload(gViewer);
    gUpload = false;
  }
  gViewer.pose(frame);
  Camera c;
  c.yaw = yaw;
  c.pitch = pitch;
  c.distance = distance;
  c.target = {tx, ty, tz};
  ViewOptions o;
  o.grid = flags & 1;
  o.axes = flags & 2;
  o.skeleton = flags & 4;
  o.textured = flags & 8;
  gView.draw(gViewer, width, height, c, o);
}

}  // extern "C"

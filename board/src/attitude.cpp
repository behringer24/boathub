#include "attitude.h"

#include <math.h>

namespace {

using attitude::Vec3;
using attitude::cross;
using attitude::dot;

// Not DEG_TO_RAD: Arduino.h defines that as a macro.
const float RAD_PER_DEG = 0.017453293f;
const float DEG_PER_RAD = 57.29578f;

// The largest zero offset the gyroscope is expected to have. The datasheet
// gives about 1 dps for this part; twice that leaves room without letting
// motion in.
const float MAX_OFFSET_DPS = 2.0f;

Vec3 add(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3 &a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float length(const Vec3 &a) { return sqrtf(dot(a, a)); }

}  // namespace

namespace attitude {

float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalised(const Vec3 &v) {
  const float len = length(v);
  return len > 0.0f ? scale(v, 1.0f / len) : v;
}

void Filter::configure(float tauS, float biasTauS) {
  tau_ = tauS > 0.1f ? tauS : 0.1f;
  biasTau_ = biasTauS > tau_ ? biasTauS : tau_;
}

void Filter::reset() {
  started_ = false;
  runS_ = 0.0f;
  up_ = Vec3{};
  bias_ = Vec3{};
  motion_ = 0.0f;
}

void Filter::update(const Vec3 &accelG, const Vec3 &gyroDps, float dtS) {
  // Well short of 1 g there is no gravity to go by: free fall, or a read that
  // returned nothing.
  const float norm = length(accelG);
  if (norm < 0.2f || dtS <= 0.0f) return;
  const Vec3 gravity = scale(accelG, 1.0f / norm);

  if (!started_) {
    up_ = gravity;
    bias_ = Vec3{};
    started_ = true;
    runS_ = 0.0f;
    motion_ = 0.0f;
    return;
  }
  runS_ += dtS;

  // 1. The gyroscope turns "up" by what the boat turned. A direction fixed in
  //    the world turns the opposite way when seen from the boat, which is
  //    d(up)/dt = up x w.
  const Vec3 w = scale(sub(gyroDps, bias_), RAD_PER_DEG);
  up_ = add(up_, scale(cross(up_, w), dtS));

  // 2. The gyroscope's offset. Uncorrected, an offset of 1 dps holds the
  //    estimate tau seconds' worth of turning away from gravity - 3 degrees at
  //    tau = 3 s. Where the gyroscope keeps carrying the estimate away, the
  //    disagreement points along the offset, so it is learned from that, over
  //    biasTau. Only the part across gravity shows; the rest does not matter
  //    here, because turning about "up" does not move "up".
  //
  //    Learning starts fast and slows down. At first the offset is wholly
  //    unknown and every reading is off by it, so for the first 10 x tau it is
  //    learned over 4 x tau - the fastest that does not overshoot. After that
  //    the time constant grows with the running time until it reaches
  //    biasTau, slow enough that a seaway averages out of it.
  //
  //    And no faster than an offset could explain. An offset of MAX_OFFSET_DPS
  //    holds the estimate at most MAX_OFFSET_DPS x tau away from gravity;
  //    anything larger is motion - a shake, a knock, the module turned over in
  //    the hand - and learning from all of it would wind the offset up, to be
  //    unlearned again over the next minute. So a larger disagreement counts
  //    as that much and no more. Capped, not discarded: a seaway pushes the
  //    disagreement far out to both sides, and dropping the far samples would
  //    keep a lopsided remainder and learn an offset that is not there.
  const float warmS = 10.0f * tau_;
  const float grown = 4.0f * tau_ + (runS_ > warmS ? runS_ - warmS : 0.0f);
  const float learnS = grown < biasTau_ ? grown : biasTau_;
  Vec3 disagreement = cross(up_, gravity);  // length: sine of the angle between them
  const float explicable = sinf(MAX_OFFSET_DPS * tau_ * RAD_PER_DEG);
  const float size = length(disagreement);
  if (size > explicable) disagreement = scale(disagreement, explicable / size);
  bias_ = add(bias_, scale(disagreement, DEG_PER_RAD * dtS / (tau_ * learnS)));

  // 3. Gravity pulls the estimate back, with time constant tau.
  const float k = dtS / (tau_ + dtS);
  up_ = normalised(add(up_, scale(sub(gravity, up_), k)));

  motion_ = length(sub(accelG, up_));
}

float heelDeg(const Vec3 &up) { return atan2f(up.y, up.z) * DEG_PER_RAD; }

float pitchDeg(const Vec3 &up) {
  return atan2f(up.x, sqrtf(up.y * up.y + up.z * up.z)) * DEG_PER_RAD;
}

void Level::set(const Vec3 &upAtRest) {
  ref_ = normalised(upAtRest);
  set_ = true;
}

// Rodrigues' rotation about ref x z, by the angle between them.
Vec3 Level::apply(const Vec3 &up) const {
  if (!set_) return up;

  const Vec3 vertical{0.0f, 0.0f, 1.0f};
  const Vec3 axis = cross(ref_, vertical);  // length: sine of the angle
  const float c = dot(ref_, vertical);      // cosine of the angle
  const float s2 = dot(axis, axis);

  if (s2 < 1e-12f) {
    // Already vertical - or upside down, which a boat is not, but the
    // arithmetic should not divide by zero over it.
    return c > 0.0f ? up : Vec3{up.x, -up.y, -up.z};
  }

  return add(add(scale(up, c), cross(axis, up)), scale(axis, dot(axis, up) * (1.0f - c) / s2));
}

}  // namespace attitude

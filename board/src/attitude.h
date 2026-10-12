// Heel and pitch from an accelerometer and a gyroscope.
//
// What is tracked is the direction of "up" as the boat sees it. The gyroscope
// turns that direction as the boat turns; the accelerometer pulls it back
// towards where gravity says it is - slowly, over a time constant tau of
// seconds, because on a boat the accelerometer also feels every wave. Heel and
// pitch are read off the result.
//
// Tracking a direction rather than two angles keeps the geometry honest: a
// heeled boat that turns swings her mast through what an angle-by-angle
// filter would take for pitch.
//
// Pure arithmetic - no hardware, no Arduino - so the bench tool and the
// firmware run exactly the same filter.
// See docs/design/A-009-imu-heel-and-motion.md section 2.

#pragma once

namespace attitude {

// Boat axes: x forward, y to port, z up. Right-handed, like the chip's own, so
// a rate of turn maps onto them exactly as an acceleration does.
//
// No default values on the members: the toolchain compiles C++11, where those
// would stop {x, y, z} from initialising it. Vec3{} is zero.
struct Vec3 {
  float x;
  float y;
  float z;
};

class Filter {
 public:
  // tauS: how long the accelerometer takes to pull the gyroscope back.
  // biasTauS: how long the gyroscope's own offset takes to be learned. It has
  // to be much longer than tau, or a seaway would be learned as offset.
  void configure(float tauS, float biasTauS);

  // Forget everything. The next sample starts it again from gravity alone.
  void reset();

  // One sample, in boat axes: acceleration in g, rate of turn in degrees per
  // second, and the time since the previous sample.
  void update(const Vec3 &accelG, const Vec3 &gyroDps, float dtS);

  bool started() const { return started_; }

  // Unit vector, boat axes.
  const Vec3 &up() const { return up_; }

  // The offset the gyroscope reads when nothing turns, as learned so far.
  const Vec3 &biasDps() const { return bias_; }

  // How far the last sample's acceleration was from gravity alone, in g: zero
  // at rest, the jolt when something moves her.
  float motionG() const { return motion_; }

 private:
  float tau_ = 3.0f;
  float biasTau_ = 60.0f;
  bool started_ = false;
  float runS_ = 0.0f;  // since the first sample
  Vec3 up_ = Vec3{};
  Vec3 bias_ = Vec3{};
  float motion_ = 0.0f;
};

// Heel and pitch of an "up" vector, in degrees. Heel is positive to
// starboard, pitch positive bow-up.
float heelDeg(const Vec3 &up);
float pitchDeg(const Vec3 &up);

// The zero reference. Records "up" with the boat at rest, and from then on
// turns every reading by the smallest rotation that would have made that one
// vertical. That corrects a tilted mounting and the boat's own trim - and
// nothing else: a module turned about the vertical stays turned, because
// gravity cannot see it.
class Level {
 public:
  void set(const Vec3 &upAtRest);
  void clear() { set_ = false; }
  bool isSet() const { return set_; }
  const Vec3 &reference() const { return ref_; }

  Vec3 apply(const Vec3 &up) const;

 private:
  bool set_ = false;
  Vec3 ref_ = Vec3{};
};

// The unit vector along v, or v unchanged if it has no length.
Vec3 normalised(const Vec3 &v);

}  // namespace attitude

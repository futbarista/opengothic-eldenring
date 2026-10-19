#pragma once

#include <Tempest/Matrix4x4>
#include <Tempest/Vec>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Skeleton;

namespace EldenCombat {
class AnimationBank;
}

// Retargets baked Elden Ring poses (ERAN bank, model-space joint positions) onto
// Gothic's Bip01 skeleton. Port of the OpenMW fork's EldenAnimation: every bone
// orientation is rebuilt from anatomical frames (limb directions, bend planes),
// calibrated once against the Gothic rest pose, so no bone-axis conventions are shared.
class EldenRetarget final {
  public:
    // Loaded once from OPENGOTHIC_ELDEN_ANIMS or player_anims.bin next to the executable.
    static std::shared_ptr<const EldenCombat::AnimationBank> bank();
    // upright two-legged Bip01 skeleton of roughly human size (humans, orcs, goblins,
    // skeletons, zombies, lizardmen...); quadrupeds and giants keep native combat
    static bool isHumanoid(const Skeleton& sk);

    bool  init(const Skeleton& sk);
    bool  isValid() const { return valid; }
    const Skeleton* skeleton() const { return skel; }
    float clipLength(std::string_view clip) const; // frames, 0 if missing
    bool  hasClip(std::string_view clip) const;

    // Sample 'clip' at 30-fps 'frame'. 'yaw' turns the whole pose around the up axis
    // (radians, positive = towards the actor's right). Crossfades over the clip's blend time.
    bool  play(std::string_view clip, float frame, bool loop, float dt, float yaw);
    // Fade back to the native Gothic animation.
    void  release(float dt);
    void  reset();

    // rotation slerp + translation lerp of two local bone matrices
    static Tempest::Matrix4x4 blend(const Tempest::Matrix4x4& a, const Tempest::Matrix4x4& b, float t);

    // Gothic's two-handed animations move the right-hand slot (so a big weapon sits between the
    // hands); under an Elden pose that offset must stay fixed to the hand instead of following them
    void  setTwoHandedSlot(bool t) { slot2H = t; }

    float weight() const { return wOverride; }
    const std::vector<Tempest::Matrix4x4>& local() const { return shownLocal; }
    const std::vector<uint8_t>&            mask()  const { return maskBones; }

  private:
    enum Joint : uint8_t {
      Pelvis, Spine, Spine1, Spine2, Neck, Head,
      LClavicle, LUpperArm, LForearm, LHand,
      RClavicle, RUpperArm, RForearm, RHand,
      LThigh, LCalf, LFoot, LToe,
      RThigh, RCalf, RFoot, RToe,
      LFinger1, LFinger2, RFinger1, RFinger2,
      JointCount
      };

    struct Mat3 {
      float m[3][3] = {{1,0,0},{0,1,0},{0,0,1}}; // m[row][col], column vectors
      static Mat3 cols(const Tempest::Vec3& a, const Tempest::Vec3& b, const Tempest::Vec3& c);
      Mat3 operator*(const Mat3& o) const;
      Tempest::Vec3 operator*(const Tempest::Vec3& v) const;
      Mat3 transposed() const;
      };

    struct Body {
      Tempest::Vec3 up, right, forward, shoulderRight, chestForward;
      };

    struct BendPlane {
      bool          ok = false;
      Tempest::Vec3 axis, normal;
      Tempest::Vec3 secondary(const Tempest::Vec3& upper, const Tempest::Vec3& lower,
                              const Tempest::Vec3& primary, const Tempest::Vec3& reference, float dt);
      };

    using Joints = std::array<Tempest::Vec3,JointCount>;

    void   samplePositions(const std::vector<float>& smp, float yaw, Joints& out, Tempest::Vec3& headUp, Tempest::Vec3& headFwd) const;
    Mat3   weaponFrame(const std::vector<float>& smp, float yaw) const;
    void   initWeaponSlot(const Skeleton& sk);
    Body   anatomy(const Joints& p) const;
    Mat3   jointFrame(size_t j, const Joints& p, const Body& b, const Tempest::Vec3& headUp, const Tempest::Vec3& headFwd,
                      std::array<Tempest::Vec3,10>* planes) const;
    static Mat3 frame(Tempest::Vec3 primary, Tempest::Vec3 secondary);
    static Mat3 rotationOf(const Tempest::Matrix4x4& m);
    static Tempest::Matrix4x4 compose(const Mat3& r, const Tempest::Vec3& t);

    std::shared_ptr<const EldenCombat::AnimationBank> eran;
    const Skeleton*                   skel  = nullptr;
    bool                              valid = false;

    std::array<size_t,JointCount>     srcOffset{};
    size_t                            srcHeadAxes = size_t(-1);
    Mat3                              toGothic;     // source basis -> skeleton model space
    float                             scale = 100.f;
    float                             srcPelvisHeight = 0;
    Tempest::Vec3                     bindUp;

    std::array<size_t,JointCount>     node;         // skeleton node per joint (or -1)
    std::vector<size_t>               jointOf;      // per node: joint or JointCount
    std::vector<Mat3>                 correction;   // per node
    std::vector<Tempest::Matrix4x4>   restLocal, restModel;
    std::vector<uint8_t>              maskBones;
    std::vector<size_t>               order;        // parents before children

    // right-hand weapon slot oriented from Elden Ring's R_Weapon bone (blade direction)
    size_t                            weaponSlot    = size_t(-1);
    size_t                            srcWeaponAxes = size_t(-1);
    Mat3                              weaponCorr;
    size_t                            handSlot = size_t(-1);
    Tempest::Matrix4x4                slot2HLocal;
    bool                              slot2H = false;

    std::array<BendPlane,10>          planes;
    std::vector<float>                smp;
    std::vector<Tempest::Matrix4x4>   targetLocal, fromLocal, shownLocal, model;

    std::string                       curClip;
    float                             curFrame = 0;
    float                             fade = 1, fadeLen = 0;
    float                             wOverride = 0;
  };

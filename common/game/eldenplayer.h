#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "eldencombat/combat.hpp"
#include "eldencombat/animations.hpp"
#include "eldencombat/aitactics.hpp"
#include "game/eldenretarget.h"
#include "graphics/mesh/animation.h"
#include "game/constants.h"

class Npc;
class Bullet;

// Elden Ring style melee for the player. The engine-independent state machine
// (common/eldencombat) decides actions, hit windows, i-frames, guard and stamina;
// this adapter plays native Gothic animations on that clock and keeps Gothic's own
// damage formula (strength, talents, weapon, protection).
class EldenPlayer final {
  public:
    enum Button : uint8_t {
      Light,
      Heavy,
      Guard,
      Dodge,
      Count
      };

    EldenPlayer();
    // Gothic animations, Elden logic: creatures keep their own clips while the controller
    // owns attack timing (from their native swing), cooldowns, dodges and poise
    explicit EldenPlayer(bool nativeAnimations) : nativeAnims(nativeAnimations) {}
    bool  usesNativeAnimations() const { return nativeAnims; }
    // true once: a hit was absorbed by poise, skip Gothic's stagger animation
    bool  consumePoiseAbsorb() { bool r = poiseAbsorbed; poiseAbsorbed = false; return r; }
    // the actor of the current player (a dummy while no world is loaded)
    static EldenPlayer& inst();

    static bool isEnabled();
    static void setEnabled(bool e);

    // input, fed by PlayerControl
    void  setButton(Button b, bool down);
    bool  isHeld(Button b) const { return btn[b].mHeld; }
    void  setMove(float x, float y);
    void  clearInput();
    void  toggleLockOn(Npc& pl, Npc* focus);
    void  clearLockOn();
    void  setDialogActive(bool d) { dialogActive = d; }
    void  setLockTarget(Npc* t) { lock = t; }
    // Elden Ring third person: the camera orbits freely, the actor turns towards its movement
    bool  freeCamera(const Npc& pl) const;
    // Elden Ring grip: any melee weapon can be held in one hand or two-handed (Y+RB / G)
    bool  toggleGrip(Npc& pl);
    bool  twoHanded(const Npc& pl);
    float gripDamageMultiplier(const Npc& pl);
    Npc*  lockTarget() const { return lock; }

    // queried by the engine
    bool  ownsMelee(const Npc& pl) const;       // elden controller owns the player's melee
    bool  blocksLocomotion(const Npc& pl) const; // native locomotion/attacks must not run now
    bool  blocksRotation(const Npc& pl) const;
    float rootMotionScale(const Npc& pl) const;
    // Gothic's strafe/turn steps move in bursts; under a smooth Elden gait loop that reads as
    // sideways jumps, so the native root motion is low-pass filtered while an Elden pose is shown
    Tempest::Vec3 smoothRootMotion(const Tempest::Vec3& dp, uint64_t dt, uint64_t tick);
    // camera orbits freely during rolls/backsteps (actor turn is locked)
    bool  cameraFree(const Npc& pl) const;
    bool  onPlayerHit(Npc& pl, Npc& attacker, const Bullet* b);
    bool  dodgesSpell(const Npc& pl) const;

    void  tick(Npc& pl, uint64_t dt);

    bool  showStamina() const;
    float stamina01() const;

    // NPC AI: Gothic's fight AI decides *what* to do, the Elden controller executes it
    bool     aiAttack(Npc& npc);
    void     aiGuard(float seconds) { aiGuardTime = std::max(aiGuardTime,seconds); }
    void     aiDodge(float x, float y);
    uint64_t aiWaitMs() const;
    const EldenCombat::Controller& controller() const { return ctrl; }

    // victim side of an Elden Ring critical (backstab/riposte)
    void     stunFor(float seconds) { stunTime = std::max(stunTime,seconds); }
    bool     isStunned() const { return stunTime>0.f; }
    void     markCriticalHit() { incomingCritical = true; }

  private:
    void     aiTick(Npc& npc, float dt);
    void     tryFinisher(Npc& pl, float dt);
    Npc*     findCriticalTarget(Npc& pl) const;
    void     updateCritical(Npc& pl);
    void     alarmWitnesses(Npc& pl, Npc& victim);

    struct Native {
      const Animation::Sequence* sq        = nullptr;
      uint64_t                   from      = 0;   // ms
      uint64_t                   to        = 0;   // ms
      uint64_t                   hit       = 0;   // ms, 0 = none
      uint16_t                   combo     = 0;
      BodyState                  bs        = BS_NONE;
      bool                       hold      = false; // guard: pin at 'hit'
      };

    bool  canRun(const Npc& pl) const;
    void  deactivate(Npc& pl);
    EldenCombat::Config makeConfig(const Npc& pl) const;
    void  applyHits(Npc& pl);
    void  driveAnimation(Npc& pl, const EldenCombat::Update& up);
    void  stopDrive(Npc& pl, bool toIdle);
    void  updateLockOn(Npc& pl, uint64_t dt);
    void  applyDisplacement(Npc& pl, const EldenCombat::Update& up);
    // Elden Ring poses (retargeted ERAN clips) shown over the native animation
    void  updatePose(Npc& pl, const EldenCombat::Update* up, float dt);
    void  releasePose(Npc& pl, float dt);
    bool  ensureRig(Npc& pl);
    bool  poseAllowed(Npc& pl) const;
    bool  actionOwnsMotion() const;
    std::optional<EldenCombat::NativeAttackTiming> nativeAttackTiming(const Npc& pl) const;

    Native  pickNative(Npc& pl, EldenCombat::Action a, const EldenCombat::Update& up) const;
    Native  segment(Npc& pl, std::string_view name, size_t seg, BodyState bs) const;
    Native  firstOf(Npc& pl, std::initializer_list<std::string_view> names, BodyState bs) const;
    std::string prefix(const Npc& pl) const;
    uint64_t mapTime(const Native& n, const EldenCombat::ActionDef& def, float frame) const;

    EldenCombat::Controller ctrl;
    EldenCombat::Button     btn[Count] = {};
    float                   moveX = 0, moveY = 0;
    bool                    active   = false;
    float                   stamina  = 100.f;
    float                   staminaShowTime = 0.f;

    Npc*                    lock     = nullptr;

    // currently driven native animation
    Native                  drv;
    EldenCombat::Action     drvAction = EldenCombat::Action::Idle;
    std::uint64_t           drvSeq    = 0;
    size_t                  lightIndex = 0;

    // per-window hit bookkeeping
    std::uint64_t           hitSeq   = 0;
    size_t                  hitIdx   = size_t(-1);
    std::vector<const Npc*> hitDone;

    EldenRetarget           rig;
    const void*             rigSkeleton = nullptr;
    float                   poseTime    = 0.f;
    std::string             poseClip;
    Tempest::Vec3           lastPos;
    bool                    hasLastPos  = false;
    float                   stridePhase = 0.f;
    float                   travelX = 0.f, travelY = 0.f; // smoothed world velocity (x,z), m/s
    bool                    gaitMoving = false;
    EldenCombat::LocomotionAnimationState gait;
    bool                    dialogActive = false;
    const void*             gripWeapon   = nullptr; // item the grip choice belongs to
    bool                    gripTwoHanded = false;
    bool                    nativeAnims   = false;
    bool                    poiseAbsorbed = false;

    EldenCombat::AiTactics  tactics;
    float                   aiGuardTime = 0.f;
    float                   lightHoldTime = 0.f;
    int                     aiTapLight = 0, aiTapHeavy = 0, aiTapDodge = 0;
    float                   aiDodgeX = 0.f, aiDodgeY = 0.f;
    Tempest::Vec3           smoothVel;       // cm per ms, actor-local
    uint64_t                smoothTick = 0;
    Tempest::Vec3           smoothLast;

    bool                    sprinting = false;
    bool                    lockedMove = false;
    bool                    driving    = false;

    Npc*                    critTarget = nullptr; // victim of the running critical
    std::uint64_t           critSeq    = 0;
    bool                    critPlaced = false;
    bool                    incomingCritical = false;
    float                   stunTime   = 0.f;
  };

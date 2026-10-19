#include "eldenplayer.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include "graphics/mesh/animation.h"
#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "world/world.h"
#include "world/waypoint.h"
#include "commandline.h"
#include "gothic.h"

using namespace Tempest;
using EAction = EldenCombat::Action;

namespace {
bool g_enabled = false;
bool debugOn() {
  static const bool v = std::getenv("OPENGOTHIC_ELDEN_DEBUG")!=nullptr || CommandLine::inst().isEldenDebug();
  return v;
  }
bool g_enabledInit = false;

bool isDodge(EAction a) {
  return a==EAction::Roll || a==EAction::CrouchRoll || a==EAction::Backstep;
  }

bool isHeavy(EAction a) {
  return a==EAction::Heavy1Charge || a==EAction::Heavy1 || a==EAction::Heavy2Charge || a==EAction::Heavy2 ||
         a==EAction::RunHeavy || a==EAction::JumpHeavy;
  }

bool isHurt(EAction a) {
  return a==EAction::HurtSmall || a==EAction::HurtMiddle || a==EAction::HurtLarge || a==EAction::Knockdown ||
         a==EAction::StanceBreak || a==EAction::GuardBreak || a==EAction::Dead;
  }

// actions left to native Gothic animation (hit reactions, death, jump)
bool isNativeOwned(EAction a) {
  switch(a) {
    case EAction::Idle:
    case EAction::Jump:
    case EAction::HurtSmall:
    case EAction::HurtMiddle:
    case EAction::HurtLarge:
    case EAction::Knockdown:
    case EAction::StanceBreak:
    case EAction::Critical:
    case EAction::Dead:
      return true;
    default:
      return false;
    }
  }
}

EldenPlayer& EldenPlayer::inst() {
  static EldenPlayer dummy;
  if(auto w = Gothic::inst().world())
    if(auto pl = w->player())
      if(auto e = pl->eldenActor())
        return *e;
  return dummy;
  }

void EldenPlayer::aiDodge(float x, float y) {
  if(aiTapDodge!=0)
    return;
  aiTapDodge = 2;
  aiDodgeX   = x;
  aiDodgeY   = y;
  }

bool EldenPlayer::aiAttack(Npc& npc) {
  if(!ownsMelee(npc) || npc.weaponState()==WeaponState::NoWeapon)
    return false;
  const bool idle      = ctrl.action()==EAction::Idle;
  const bool attacking = EldenCombat::Controller::isAttack(ctrl.action());
  if(idle && !tactics.attackReady())
    return false;
  if(!idle && !(attacking && tactics.canCombo(ctrl,EldenCombat::AiAttackButton::Light,3)))
    return false;
  if(ctrl.stamina()<=0.f)
    return false;
  const bool heavy = idle && npc.world().script().rand(100)<20;
  if(heavy)
    aiTapHeavy = 2; else
    aiTapLight = 2;
  if(attacking)
    tactics.markComboRequested(ctrl);
  return true;
  }

uint64_t EldenPlayer::aiWaitMs() const {
  if(!EldenCombat::Controller::isAttack(ctrl.action()))
    return 300;
  const auto& def    = ctrl.definition();
  const float frames = std::max(def.mCancelLight-ctrl.frame()-6.f,3.f);
  return uint64_t(std::clamp(frames/EldenCombat::AnimationFps*1000.f,100.f,1200.f));
  }

void EldenPlayer::aiTick(Npc& npc, float dt) {
  auto tap = [&](int& t, Button b) {
    if(t==2) {
      setButton(b,true);
      t = 1;
      }
    else if(t==1) {
      setButton(b,false);
      t = 0;
      }
    };
  setButton(Guard,aiGuardTime>0.f);
  aiGuardTime = std::max(0.f,aiGuardTime-dt);
  tap(aiTapLight,Light);
  tap(aiTapHeavy,Heavy);
  tap(aiTapDodge,Dodge);

  tactics.update(ctrl,dt,0.45f);

  // Elden-style reaction: sometimes dodge the player's swing instead of eating it
  auto tg = npc.target();
  if(!nativeAnims && tg!=nullptr && tg->isPlayer() && ownsMelee(npc) && npc.weaponState()!=WeaponState::NoWeapon) {
    auto te = tg->eldenActorIfAny();
    if(te!=nullptr && te->ownsMelee(*tg)) {
      tactics.observeThreat(te->controller(),dt);
      const float dist = (tg->position()-npc.position()).length();
      if(dist<400.f && ctrl.action()==EAction::Idle && tactics.consumeReaction(0.18f)) {
        const int r = int(npc.world().script().rand(100));
        // like Elden Ring enemies: only humans roll and backstep, creatures and orcs guard or take it
        if(r<15 && !npc.isMonster())
          aiDodge(0.f,0.f);         // backstep
        else if(r<30 && !npc.isMonster())
          aiDodge(r%2==0 ? 1.f : -1.f, 0.f); // side roll
        else if(r<45)
          aiGuard(0.5f);
        }
      }
    }
  }

EldenPlayer::EldenPlayer() {
  }

bool EldenPlayer::isEnabled() {
  if(!g_enabledInit) {
    g_enabledInit = true;
    g_enabled     = CommandLine::inst().isEldenCombat();
    }
  return g_enabled;
  }

void EldenPlayer::setEnabled(bool e) {
  g_enabledInit = true;
  g_enabled     = e;
  }

void EldenPlayer::setButton(Button b, bool down) {
  auto& s = btn[b];
  if(down && !s.mHeld)
    s.mPressed = true;
  if(!down && s.mHeld)
    s.mReleased = true;
  s.mHeld = down;
  }

void EldenPlayer::setMove(float x, float y) {
  moveX = std::clamp(x,-1.f,1.f);
  moveY = std::clamp(y,-1.f,1.f);
  }

void EldenPlayer::clearInput() {
  for(auto& b:btn) {
    if(b.mHeld)
      b.mReleased = true;
    b.mHeld    = false;
    b.mPressed = false;
    }
  moveX = 0;
  moveY = 0;
  }

void EldenPlayer::clearLockOn() {
  lock = nullptr;
  }

void EldenPlayer::toggleLockOn(Npc& pl, Npc* focus) {
  if(lock!=nullptr) {
    lock = nullptr;
    return;
    }
  if(focus!=nullptr && !focus->isDown() && focus!=&pl) {
    lock = focus;
    return;
    }

  auto&       w    = pl.world();
  const float a    = pl.rotationRad();
  const Vec3  fwd  = Vec3(std::cos(a),0,std::sin(a));
  Npc*        best = nullptr;
  float       bestScore = 0;
  for(uint32_t i=0; i<w.npcCount(); ++i) {
    auto npc = w.npcById(i);
    if(npc==nullptr || npc==&pl || npc->isDown())
      continue;
    auto  dp   = npc->position()-pl.position();
    dp.y = 0;
    float dist = dp.length();
    if(dist>1500.f || dist<1.f)
      continue;
    float dot = Vec3::dotProduct(dp/dist,fwd);
    if(dot<0.25f)
      continue;
    float score = dot*2.f - dist/1500.f + (npc->target()==&pl ? 1.f : 0.f);
    if(best==nullptr || score>bestScore) {
      best      = npc;
      bestScore = score;
      }
    }
  lock = best;
  if(debugOn())
    Log::i("elden: lock-on ",lock ? std::string(lock->displayName()) : std::string("<none>"));
  }

bool EldenPlayer::canRun(const Npc& pl) const {
  if(!isEnabled())
    return false;
  const auto ws = pl.weaponState();
  // unarmed: rolls, backsteps and sprint only (as in Elden Ring); bows/magic stay native
  if(ws!=WeaponState::NoWeapon && ws!=WeaponState::Fist && ws!=WeaponState::W1H && ws!=WeaponState::W2H)
    return false;
  if(pl.isDown() || pl.interactive()!=nullptr || pl.isSwim() || pl.isDive())
    return false;
  if(pl.isTalk() || dialogActive)
    return false;
  if(pl.isPlayer() && !pl.isAiQueueEmpty())
    return false; // scripted player movement
  if(ws==WeaponState::NoWeapon && pl.eldenIsSwitchingWeapon())
    return false;
  return true;
  }

bool EldenPlayer::ownsMelee(const Npc& pl) const {
  return active && canRun(pl);
  }

bool EldenPlayer::blocksLocomotion(const Npc& pl) const {
  if(!ownsMelee(pl))
    return false;
  return driving || lockedMove || actionOwnsMotion();
  }

bool EldenPlayer::actionOwnsMotion() const {
  // an Elden action moves the actor itself, with or without a matching native animation
  return active && !isNativeOwned(ctrl.action());
  }

bool EldenPlayer::blocksRotation(const Npc& pl) const {
  if(!ownsMelee(pl))
    return false;
  return lock!=nullptr || isDodge(ctrl.action());
  }

float EldenPlayer::rootMotionScale(const Npc& pl) const {
  if(!isEnabled())
    return 1.f;
  if(!ownsMelee(pl))
    return sprinting ? 1.35f : 1.f;
  if(nativeAnims)
    return 1.f; // creature clips carry their own root motion
  if(driving || actionOwnsMotion())
    return 0.f; // elden controller owns displacement
  return ctrl.sprinting() ? 1.5f : 1.f;
  }

Vec3 EldenPlayer::smoothRootMotion(const Vec3& dp, uint64_t dt, uint64_t tick) {
  // only under a looping Elden gait; actions own their displacement
  if(rig.weight()<=0.f || driving || dt==0 || ctrl.action()!=EAction::Idle) {
    smoothVel  = dp/float(std::max<uint64_t>(dt,1));
    smoothTick = tick;
    smoothLast = dp;
    return dp;
    }
  if(tick==smoothTick)
    return smoothLast; // queried twice in one tick
  const Vec3  v = dp/float(dt);
  const float a = 1.f-std::exp(-float(dt)/1000.f*8.f);
  smoothVel  = smoothVel + (v-smoothVel)*a;
  smoothTick = tick;
  smoothLast = Vec3(smoothVel.x*float(dt),dp.y,smoothVel.z*float(dt));
  return smoothLast;
  }

bool EldenPlayer::cameraFree(const Npc& pl) const {
  if(freeCamera(pl))
    return lock==nullptr;
  return ownsMelee(pl) && isDodge(ctrl.action());
  }

bool EldenPlayer::freeCamera(const Npc& pl) const {
  if(!isEnabled() || !pl.isPlayer() || dialogActive)
    return false;
  const auto ws = pl.weaponState();
  if(ws==WeaponState::Bow || ws==WeaponState::CBow || ws==WeaponState::Mage)
    return false; // aiming keeps the classic camera-bound facing
  if(pl.interactive()!=nullptr || pl.isSwim() || pl.isDive() || pl.isDown())
    return false;
  return true;
  }

bool EldenPlayer::twoHanded(const Npc& pl) {
  const auto ws = pl.weaponState();
  if(ws!=WeaponState::W1H && ws!=WeaponState::W2H)
    return false;
  const Item* w = pl.inventory().activeWeapon();
  if(w!=gripWeapon) {
    // new weapon: start with its native grip
    gripWeapon    = w;
    gripTwoHanded = (w!=nullptr && w->is2H());
    }
  return gripTwoHanded;
  }

bool EldenPlayer::toggleGrip(Npc& pl) {
  if(!ownsMelee(pl) || ctrl.action()!=EAction::Idle)
    return false;
  const auto ws = pl.weaponState();
  if(ws!=WeaponState::W1H && ws!=WeaponState::W2H)
    return false;
  gripTwoHanded = !twoHanded(pl);
  for(auto b:{Light,Heavy,Guard})
    btn[b] = EldenCombat::Button();
  Gothic::inst().onPrint(gripTwoHanded ? "Chwyt oburęczny" : "Chwyt jednoręczny");
  return true;
  }

float EldenPlayer::gripDamageMultiplier(const Npc& pl) {
  const Item* w = pl.inventory().activeWeapon();
  if(w==nullptr)
    return 1.f;
  const bool two = twoHanded(pl);
  if(w->is2H())
    return two ? 1.f : 0.75f; // a great weapon in one hand swings weaker
  return two ? 1.25f : 1.f;   // both hands on a one-handed weapon hit harder
  }

void EldenPlayer::tryFinisher(Npc& pl, float dt) {
  // hold light attack next to a knocked-out enemy: Gothic's finishing move
  lightHoldTime = btn[Light].mHeld ? lightHoldTime+dt : 0.f;
  if(lightHoldTime<0.4f || ctrl.action()!=EAction::Idle && !EldenCombat::Controller::isAttack(ctrl.action()))
    return;
  const auto ws = pl.weaponState();
  if(ws!=WeaponState::W1H && ws!=WeaponState::W2H)
    return;
  auto& w = pl.world();
  Npc*  best = nullptr;
  float bestD = 0;
  for(uint32_t i=0; i<w.npcCount(); ++i) {
    auto npc = w.npcById(i);
    if(npc==nullptr || npc==&pl || !npc->isUnconscious() || !pl.canFinish(*npc))
      continue;
    const float d = (npc->position()-pl.position()).quadLength();
    if(best==nullptr || d<bestD) {
      best  = npc;
      bestD = d;
      }
    }
  if(best==nullptr)
    return;
  ctrl.cancelInputActions();
  if(driving)
    stopDrive(pl,false);
  btn[Light] = EldenCombat::Button();
  lightHoldTime = 0.f;
  pl.setTarget(best);
  auto dp = best->position()-pl.position();
  pl.setDirection(float(std::atan2(dp.z,dp.x)*180.0/3.14159265358979323846));
  pl.finishingMove();
  }

bool EldenPlayer::showStamina() const {
  return isEnabled() && (active || stamina<99.5f || staminaShowTime>0.f);
  }

float EldenPlayer::stamina01() const {
  return std::clamp(stamina/100.f,0.f,1.f);
  }

std::string EldenPlayer::prefix(const Npc& pl) const {
  switch(pl.weaponState()) {
    case WeaponState::W1H: return "1H";
    case WeaponState::W2H: return "2H";
    case WeaponState::NoWeapon: return "";
    default:               return "FIST";
    }
  }

EldenCombat::Config EldenPlayer::makeConfig(const Npc& pl) const {
  using EldenCombat::WeaponClass;
  EldenCombat::Config cfg;
  cfg.mMaximumStamina           = 100.f;
  cfg.mMaximumStance            = 60.f;
  cfg.mPostHitProtectionSeconds = 6.f/EldenCombat::AnimationFps;
  // Gothic parries with the weapon itself: treat it as a guarding off-hand
  cfg.mHasShield                = true;
  cfg.mHasLeftWeapon            = false;
  cfg.mLoad                     = EldenCombat::Load::Medium;

  cfg.mWeaponClass = WeaponClass::Fist;
  if(auto w = pl.inventory().activeWeapon()) {
    const int32_t fl = w->itemFlag();
    if(fl & ITM_2HD_AXE)
      cfg.mWeaponClass = WeaponClass::Greataxe;
    else if(fl & ITM_2HD_SWD)
      cfg.mWeaponClass = WeaponClass::Greatsword;
    else if(fl & ITM_AXE)
      cfg.mWeaponClass = WeaponClass::Axe;
    else if(fl & ITM_DAG)
      cfg.mWeaponClass = WeaponClass::Dagger;
    else
      cfg.mWeaponClass = WeaponClass::StraightSword;
    }
  if(pl.weaponState()==WeaponState::Fist)
    cfg.mWeaponClass = WeaponClass::Fist;

  if(!pl.isPlayer()) {
    // Elden Ring enemies have poise: light hits wear it down before they stagger
    EldenCombat::PoiseStats ps;
    ps.mStrength    = float(pl.attribute(ATR_STRENGTH));
    ps.mEndurance   = 40.f + float(pl.level())*1.5f;
    ps.mHealth      = float(pl.attribute(ATR_HITPOINTSMAX));
    ps.mLevel       = float(pl.level());
    ps.mCreature    = pl.isMonster();
    ps.mArmorRating = float(std::max(pl.protection(PROT_EDGE),pl.protection(PROT_BLUNT)));
    cfg.mMaximumPoise = EldenCombat::poiseFromStats(ps);
    }
  if(nativeAnims) {
    cfg.mHasShield      = false;
    cfg.mWeaponClass    = WeaponClass::Fist;
    cfg.mMaximumStamina = 1000.f; // creatures are limited by cooldowns, not stamina
    cfg.mNativeAttack   = nativeAttackTiming(pl);
    }
  return cfg;
  }

std::optional<EldenCombat::NativeAttackTiming> EldenPlayer::nativeAttackTiming(const Npc& pl) const {
  // the creature's own swing: its length and optimal (contact) frame drive the hit window
  for(std::string_view name:{"S_FISTATTACK","T_FISTATTACKMOVE"}) {
    auto sq = pl.eldenFindAnim(name);
    if(sq==nullptr || sq->data==nullptr)
      continue;
    auto& d = *sq->data;
    float endMs = sq->totalTime();
    if(!d.defHitEnd.empty())
      endMs = float(d.defHitEnd[0]);
    float hitMs = -1.f;
    for(auto& e:d.events)
      if(e.type==zenkit::MdsEventType::OPTIMAL_FRAME && !e.frames.empty()) {
        hitMs = float(e.frames[0])*1000.f/d.fpsRate;
        break;
        }
    if(hitMs<0.f || endMs<=hitMs)
      continue;
    EldenCombat::NativeAttackTiming t;
    t.mAnimation = std::string(name);
    t.mDuration  = endMs*EldenCombat::AnimationFps/1000.f;
    t.mHitFrame  = hitMs*EldenCombat::AnimationFps/1000.f;
    return t;
    }
  return std::nullopt;
  }

void EldenPlayer::deactivate(Npc& pl) {
  if(!active)
    return;
  if(driving)
    stopDrive(pl,false);
  ctrl.cancelInputActions();
  ctrl.reset();
  active     = false;
  lockedMove = false;
  }

// Dev helper (-eldentestchar): a strong one-handed fighter near the orcs of the Mining Valley,
// saved to slot 5. Start with "-w OldWorld.zen -nomenu".
static void eldenTestCharacter(Npc& pl) {
  static int stage = 0;
  static uint64_t t0 = 0;
  if(stage>=3 || !CommandLine::inst().isEldenTestChar() || !pl.isPlayer())
    return;
  auto& w   = pl.world();
  auto& scr = w.script();
  const uint64_t now = w.tickCount();
  if(stage==0) {
    t0    = now;
    stage = 1;
    return;
    }
  if(stage==1 && now>t0+3000) {
    auto& h = pl.handle();
    h.level    = 30;
    h.exp      = 500*30*31/2;
    h.exp_next = 500*31*32/2;
    h.lp       = 20;
    h.attribute[ATR_STRENGTH]     = 160;
    h.attribute[ATR_DEXTERITY]    = 80;
    h.attribute[ATR_HITPOINTSMAX] = 700;
    h.attribute[ATR_HITPOINTS]    = 700;
    h.attribute[ATR_MANAMAX]      = 60;
    h.attribute[ATR_MANA]         = 60;
    h.hitchance[TALENT_1H] = 90;
    h.hitchance[TALENT_2H] = 60;
    pl.setTalentSkill(TALENT_1H,2);
    pl.setTalentValue(TALENT_1H,90);
    pl.setTalentSkill(TALENT_2H,1);
    pl.setTalentValue(TALENT_2H,60);

    auto give = [&](std::string_view name, size_t n, bool equip) {
      const size_t id = scr.findSymbolIndex(name);
      if(id==size_t(-1)) {
        Log::e("elden testchar: no item ",std::string(name));
        return;
        }
      pl.addItem(id,n);
      if(equip)
        pl.useItem(id);
      };
    give("ITAR_DJG_H",1,true);
    give("ITMW_1H_SPECIAL_04",1,true);
    give("ITPO_HEALTH_03",15,false);
    give("ITPO_HEALTH_ADDON_04",3,false);

    // a little away from an orc camp
    const WayPoint* orc = nullptr;
    for(auto n:{"OW_ORC_LOOKOUT_2_02","OW_ORC_LOOKOUT_2_03","OW_ORC_ORCDOG_SPAWN01"}) {
      orc = w.findWayPoint(n);
      if(orc!=nullptr)
        break;
      }
    if(orc!=nullptr) {
      const auto at   = orc->position();
      auto       near = w.findWayPoint(at,[&](const WayPoint& p) {
        const float d = (p.position()-at).length();
        return d>1800.f && d<3500.f;
        });
      pl.setPosition(near!=nullptr ? near->position() : at);
      pl.setDirection(at);
      Log::i("elden testchar: placed near ",orc->name);
      } else {
      Log::e("elden testchar: no orc waypoint found");
      }
    w.setDayTime(10,0); // daylight
    Gothic::inst().onPrint("Elden test: postać gotowa, zapis za chwilę (slot 5)");
    t0    = now;
    stage = 2;
    return;
    }
  if(stage==2 && now>t0+2500) {
    Gothic::inst().save("save_slot_5.sav","Elden: Gornicza Dolina, orki");
    Log::i("elden testchar: saved to save_slot_5.sav");
    stage = 3;
    }
  }

void EldenPlayer::tick(Npc& pl, uint64_t dt) {
  eldenTestCharacter(pl);
  const float dtF = float(dt)/1000.f;
  staminaShowTime = std::max(0.f,staminaShowTime-dtF);

  // validate lock-on target (npc may be gone or dead)
  if(lock!=nullptr) {
    bool found = false;
    auto& w = pl.world();
    for(uint32_t i=0; i<w.npcCount(); ++i)
      if(w.npcById(i)==lock) {
        found = true;
        break;
        }
    if(!found || lock->isDown() || (lock->position()-pl.position()).length()>2500.f || !isEnabled())
      lock = nullptr;
    }

  if(!canRun(pl)) {
    deactivate(pl);
    // out of combat: hold dodge to sprint, free of stamina (as in Elden Ring)
    const bool moving = std::hypot(moveX,moveY)>0.25f;
    sprinting = isEnabled() && btn[Dodge].mHeld && moving && !pl.isInAir() && !pl.isSwim() && !pl.isDive()
                && !pl.isDown() && pl.interactive()==nullptr && pl.isAiQueueEmpty();
    updatePose(pl,nullptr,dtF);
    stamina   = std::min(100.f,stamina+45.f*dtF);
    for(auto& b:btn) {
      b.mPressed  = false;
      b.mReleased = false;
      }
    return;
    }
  sprinting = false;

  if(!active) {
    ctrl.reset();
    ctrl.configure(makeConfig(pl));
    ctrl.setVitals(stamina,100.f);
    active = true;
    }
  ctrl.configure(makeConfig(pl));

  if(!pl.isPlayer())
    aiTick(pl,dtF);
  else
    tryFinisher(pl,dtF);

  EldenCombat::Input in;
  const bool unarmed = pl.weaponState()==WeaponState::NoWeapon;
  if(!unarmed) {
    in.mLight    = btn[Light];
    in.mHeavy    = btn[Heavy];
    in.mGuard    = btn[Guard];
    }
  in.mDodge      = btn[Dodge];
  in.mMoveX      = moveX;
  in.mMoveY      = moveY;
  in.mInCombat   = !unarmed; // out-of-combat sprint is free
  in.mGrounded   = !(pl.isInAir() || pl.isFalling() || pl.isJump() || pl.isJumpUp() || pl.isSlide());
  in.mCrouching  = (pl.walkMode() & WalkBit::WM_Sneak)==WalkBit::WM_Sneak;
  in.mLockedOn   = lock!=nullptr;
  in.mTwoHanded  = twoHanded(pl);
  if(!pl.isPlayer() && btn[Dodge].mReleased)
    in.mDodgeDirection = std::array<float,2>{aiDodgeX,aiDodgeY};

  const auto up = ctrl.update(in,dtF);
  for(auto& b:btn) {
    b.mPressed  = false;
    b.mReleased = false;
    }
  stamina    = ctrl.stamina();
  lockedMove = up.mMovementLocked;
  if(up.mStaminaSpent>0.f)
    staminaShowTime = 3.f;

  applyHits(pl);
  driveAnimation(pl,up);
  applyDisplacement(pl,up);
  updateLockOn(pl,dt);
  updatePose(pl,&up,dtF);
  }

void EldenPlayer::releasePose(Npc& pl, float dt) {
  hasLastPos = false;
  travelX    = 0.f;
  travelY    = 0.f;
  gaitMoving = false;
  if(rig.weight()<=0.f)
    return;
  rig.release(dt);
  if(rig.weight()>0.f)
    pl.eldenSetBoneOverride(rig.local(),rig.mask(),rig.weight()); else
    pl.eldenClearBoneOverride();
  poseClip.clear();
  }

bool EldenPlayer::ensureRig(Npc& pl) {
  // (re)bind the rig to the player's skeleton
  auto sk = pl.eldenSkeleton();
  if(sk==nullptr)
    return false;
  if(rigSkeleton!=sk) {
    rigSkeleton = sk;
    rig.init(*sk);
    }
  return rig.isValid();
  }

bool EldenPlayer::poseAllowed(Npc& pl) const {
  if(!isEnabled() || dialogActive)
    return false;
  const auto ws = pl.weaponState();
  if(ws!=WeaponState::NoWeapon && ws!=WeaponState::Fist && ws!=WeaponState::W1H && ws!=WeaponState::W2H)
    return false; // bows, crossbows and magic keep their native animations
  if(pl.interactive()!=nullptr || pl.isSwim() || pl.isDive() || pl.isTalk())
    return false;
  if(pl.isPlayer() && !pl.isAiQueueEmpty())
    return false;
  if(pl.eldenIsSwitchingWeapon())
    return false;
  if(!pl.isPlayer()) {
    // NPCs: only near the player, to keep the cost bounded
    auto& w = pl.world();
    auto  p = w.player();
    if(p==nullptr || (p->position()-pl.position()).quadLength()>3000.f*3000.f)
      return false;
    }
  if(active && ctrl.action()!=EAction::Idle)
    return true; // combat states (hit, parade, stumble) are handled by the controller
  if(!pl.eldenNativeLocomotion())
    return false; // ambient/gesture animations stay native
  const auto bs = pl.bodyStateMasked();
  return bs==BS_STAND || bs==BS_WALK || bs==BS_RUN || bs==BS_SPRINT || bs==BS_SNEAK || bs==BS_NONE;
  }

void EldenPlayer::updatePose(Npc& pl, const EldenCombat::Update* up, float dt) {
  if(nativeAnims) {
    releasePose(pl,dt);
    return;
    }
  if(!poseAllowed(pl) || !ensureRig(pl)) {
    releasePose(pl,dt);
    return;
    }

  using EldenCombat::WeaponClass;
  const bool    armed    = active;
  const EAction a        = armed ? ctrl.action() : EAction::Idle;
  const bool    twoH     = armed && twoHanded(pl);
  const auto    weapon   = armed ? ctrl.config().mWeaponClass : WeaponClass::Fist;
  const bool    grounded = !(pl.isInAir() || pl.isFalling() || pl.isJump() || pl.isJumpUp() || pl.isSlide());
  const float   advance  = (up!=nullptr && up->mFrozen) ? 0.f : dt;
  const bool    sprint   = armed ? ctrl.sprinting() : sprinting;

  // actual horizontal travel in actor space (x right, y forward), m/s, smoothed:
  // gait direction/speed follow what Gothic really moves, not the stick
  const Vec3 pos = pl.position();
  if(hasLastPos && dt>0.f) {
    Vec3 d = pos-lastPos;
    const float k  = 1.f-std::exp(-dt*12.f);
    travelX += (d.x/dt/100.f-travelX)*k;
    travelY += (d.z/dt/100.f-travelY)*k;
    }
  lastPos    = pos;
  hasLastPos = true;
  const float speed = std::hypot(travelX,travelY);
  // the pose faces the lock-on target while the actor runs around it (strafe/back in 8 directions)
  float faceRad = pl.rotationRad();
  if(lock!=nullptr && freeCamera(pl)) {
    auto dp = lock->position()-pl.position();
    if(std::abs(dp.x)+std::abs(dp.z)>1.f)
      faceRad = std::atan2(dp.z,dp.x);
    }
  // travel in the facing frame (x right, y forward)
  const float relX = travelX*std::sin(faceRad) - travelY*std::cos(faceRad);
  const float relY = travelX*std::cos(faceRad) + travelY*std::sin(faceRad);
  float faceYaw = pl.rotationRad()-faceRad; // radians towards the actor's right
  faceYaw = std::remainder(faceYaw,6.2831853f);

  std::string clip;
  float       frame = 0.f;
  bool        loop  = false;
  float       yaw   = 0.f;

  if(a==EAction::Dead || a==EAction::Jump || pl.isDown()) {
    releasePose(pl,dt);
    return;
    }
  if(a!=EAction::Idle) {
    gait.reset();
    const auto& def = ctrl.definition();
    clip = def.mAnimation;
    if(a==EAction::StanceBreak)
      clip = EldenCombat::Controller::actionDefinition(EAction::GuardBreak,ctrl.config()).mAnimation;
    frame = ctrl.frame()*def.mAnimationSpeed;
    if(a==EAction::Roll || a==EAction::CrouchRoll)
      yaw = up!=nullptr ? std::atan2(up->mMoveX,up->mMoveY) : 0.f;
    poseTime = 0.f;
    }
  else if(!grounded) {
    releasePose(pl,dt); // jumps and falls stay native
    return;
    }
  else if(armed && ctrl.guarding()) {
    gait.reset();
    poseTime += advance;
    loop = true;
    // no shields in Gothic: guard with the weapon held like a two-handed grip
    clip = EldenCombat::stanceAnimation(weapon,true,100);
    if(!rig.hasClip(clip))
      clip = EldenCombat::stanceAnimation(weapon,twoH,100);
    if(!rig.hasClip(clip))
      clip = EldenCombat::stanceAnimation(WeaponClass::Shield,false,100);
    frame = poseTime*EldenCombat::AnimationFps;
    }
  else {
    // locomotion as in the OpenMW fork: directional walk/run/crouch loops, forward sprint
    poseTime += advance;
    loop = true;
    const bool crouch = (pl.walkMode() & WalkBit::WM_Sneak)==WalkBit::WM_Sneak;
    // hysteresis: start at 0.3 m/s, keep the gait down to 0.1 m/s (no idle pose while sliding)
    gaitMoving = gaitMoving ? speed>0.1f : speed>0.3f;
    if(gaitMoving) {
      const float dirX = relX/std::max(speed,1e-3f);
      const float dirY = relY/std::max(speed,1e-3f);
      const bool running = (pl.walkMode() & WalkBit::WM_Walk)!=WalkBit::WM_Walk;
      const auto g = gait.update(dirX,dirY,crouch,running,sprint && !crouch,false);
      yaw  = (sprint && !crouch) ? std::atan2(dirX,std::max(dirY,-0.99f)) : -g.mYaw;
      yaw += faceYaw;
      clip = EldenCombat::stanceAnimation(weapon,twoH,g.mId);
      if(crouch || !rig.hasClip(clip))
        clip = EldenCombat::stanceAnimation(WeaponClass::Fist,false,g.mId);
      const float len = rig.clipLength(clip);
      if(len>1.f && g.mNativeSpeed>0.f) {
        // retain the stride phase across gait and direction changes
        // Gothic travels slower than Elden Ring: play the gait a bit faster than the pure
        // speed ratio, never slower than 85% of the authored rate (no slow-motion look)
        const float rate = std::clamp(speed/g.mNativeSpeed*1.25f,0.85f,1.6f);
        stridePhase = std::fmod(stridePhase + rate*EldenCombat::AnimationFps*advance/(len-1.f), 1.f);
        frame = stridePhase*(len-1.f);
        }
      } else {
      gait.reset();
      yaw   = faceYaw;
      clip  = crouch ? "a000_300000" : EldenCombat::stanceAnimation(weapon,twoH,0);
      frame = poseTime*EldenCombat::AnimationFps;
      }
    }

  bool played = false;
  rig.setTwoHandedSlot(pl.weaponState()==WeaponState::W2H);
  try {
    played = !clip.empty() && rig.play(clip,frame,loop,advance,yaw);
    }
  catch(const std::exception& e) {
    Log::e("elden: pose ",clip," failed: ",e.what());
    played = false;
    }
  if(!played) {
    if(debugOn() && !clip.empty() && clip!=poseClip)
      Log::i("elden: no Elden Ring clip ",clip," (action ",int(a),"), native pose");
    poseClip = clip;
    releasePose(pl,dt);
    return;
    }
  if(debugOn() && clip!=poseClip)
    Log::i("elden: pose ",clip," action ",int(a));
  poseClip = clip;
  static int trace = std::getenv("OPENGOTHIC_ELDEN_TRACE")!=nullptr ? 600 : 0;
  if(trace>0) {
    --trace;
    Log::i("elden: trace clip=",clip," frame=",frame," w=",rig.weight());
    }
  pl.eldenSetBoneOverride(rig.local(),rig.mask(),rig.weight());
  }

void EldenPlayer::applyHits(Npc& pl) {
  auto& w = pl.world();
  while(auto h = ctrl.activeHit()) {
    if(h->mSequence!=hitSeq || h->mHitIndex!=hitIdx) {
      hitSeq = h->mSequence;
      hitIdx = h->mHitIndex;
      hitDone.clear();
      }

    float mul = std::clamp(h->mDamageMultiplier,0.5f,2.5f)*gripDamageMultiplier(pl);
    if(isHeavy(h->mAction))
      mul *= 1.5f;
    if(h->mAction==EAction::Heavy1Charge || h->mAction==EAction::Heavy2Charge)
      mul *= 1.25f; // swing released at full charge
    if(h->mAction==EAction::GuardCounter)
      mul *= 1.3f;

    bool any = false;
    for(uint32_t i=0; i<w.npcCount(); ++i) {
      auto npc = w.npcById(i);
      if(npc==nullptr || npc==&pl || npc->isDown())
        continue;
      // only the focused/locked target and npcs fighting the player: don't cleave through bystanders
      // the focused/locked target, anyone fighting this actor, and (for the player) hostile creatures
      bool relevant = (npc==lock || npc==pl.target() || npc->target()==&pl);
      if(!relevant && pl.isPlayer()) {
        // Elden Ring: the player's swing hits whoever stands in its arc, except allies busy
        // fighting someone else next to you (paladins at your side against orcs)
        const bool allyInBattle = !npc->isMonster() && npc->target()!=nullptr && npc->target()!=&pl &&
                                  npc->weaponState()!=WeaponState::NoWeapon &&
                                  w.script().personAttitude(*npc,pl)!=ATT_HOSTILE;
        relevant = !allyInBattle;
        }
      if(!relevant)
        continue;
      if(std::find(hitDone.begin(),hitDone.end(),npc)!=hitDone.end())
        continue;
      if(!pl.eldenCanHit(*npc))
        continue;
      pl.eldenHit(*npc,mul);
      hitDone.push_back(npc);
      if(debugOn())
        Log::i("elden: ",std::string(pl.displayName())," hit ",std::string(npc->displayName())," x",mul,
               " hp=",npc->attribute(ATR_HITPOINTS),npc->isImmortal() ? " (immortal)" : "");
      any = true;
      }

    if(!any)
      break; // keep the window open: a target may still step into range
    ctrl.applyHitStop(std::clamp(h->mHitStop,0.f,0.12f));
    ctrl.markHit();
    }
  }

EldenPlayer::Native EldenPlayer::segment(Npc& pl, std::string_view name, size_t seg, BodyState bs) const {
  Native n;
  n.sq = pl.eldenFindAnim(name);
  n.bs = bs;
  if(n.sq==nullptr || n.sq->data==nullptr)
    return Native();

  auto&          d     = *n.sq->data;
  const uint64_t total = uint64_t(std::max(1.f,n.sq->totalTime()));
  const size_t   cnt   = std::max<size_t>(1,d.defHitEnd.size());
  seg = seg%cnt;

  n.from  = 0;
  n.to    = total;
  n.combo = 0;
  if(!d.defHitEnd.empty()) {
    n.from  = seg==0 ? 0 : d.defHitEnd[seg-1];
    n.to    = (seg+1<d.defHitEnd.size()) ? d.defHitEnd[seg] : total;
    n.combo = uint16_t(seg);
    }
  n.to = std::max(n.to,n.from+1);

  for(auto& e:d.events) {
    if(e.type!=zenkit::MdsEventType::OPTIMAL_FRAME)
      continue;
    for(auto f:e.frames) {
      const uint64_t t = uint64_t(float(std::max(0,f))*1000.f/d.fpsRate);
      if(n.from<t && t<n.to) {
        n.hit = t;
        break;
        }
      }
    }
  return n;
  }

EldenPlayer::Native EldenPlayer::firstOf(Npc& pl, std::initializer_list<std::string_view> names, BodyState bs) const {
  for(auto name:names) {
    auto n = segment(pl,name,0,bs);
    if(n.sq!=nullptr)
      return n;
    }
  return Native();
  }

EldenPlayer::Native EldenPlayer::pickNative(Npc& pl, EAction a, const EldenCombat::Update& up) const {
  // creature swings: the definition already carries the native clip and its timing
  if(EldenCombat::Controller::isAttack(a) && !ctrl.definition().mNativeAnimation.empty())
    return segment(pl,ctrl.definition().mNativeAnimation,0,BS_HIT);
  const std::string p  = prefix(pl);
  const std::string sAttack   = "S_"+p+"ATTACK";
  const std::string tAttackL  = "T_"+p+"ATTACKL";
  const std::string tAttackR  = "T_"+p+"ATTACKR";
  const std::string tAttackMv = "T_"+p+"ATTACKMOVE";
  const std::string tParade   = "T_"+p+"PARADE_0";
  const std::string tParadeHt = "T_"+p+"PARADE_0_A2";
  const std::string tJumpB    = "T_"+p+"PARADEJUMPB";
  const std::string tJumpB2   = "T_"+p+"JUMPB";
  const std::string tStrafeL  = "T_"+p+"RUNSTRAFEL";
  const std::string tStrafeR  = "T_"+p+"RUNSTRAFER";

  auto light = [&](size_t idx) -> Native {
    auto base = segment(pl,sAttack,0,BS_HIT);
    if(base.sq==nullptr)
      return firstOf(pl,{tAttackL,tAttackR},BS_HIT);
    const size_t segs = std::max<size_t>(1,base.sq->data->defHitEnd.size());
    if(segs>=2)
      return segment(pl,sAttack,idx%segs,BS_HIT);
    // single-hit combo (low talent): alternate with side swings
    switch(idx%3) {
      case 0: return base;
      case 1: { auto n = firstOf(pl,{tAttackL},BS_HIT); return n.sq ? n : base; }
      default:{ auto n = firstOf(pl,{tAttackR},BS_HIT); return n.sq ? n : base; }
      }
    };

  switch(a) {
    case EAction::Light1: case EAction::Light2: case EAction::Light3:
    case EAction::Light4: case EAction::Light5: case EAction::Light6:
      return light(size_t(a)-size_t(EAction::Light1));
    case EAction::LeftLight1: case EAction::LeftLight2: case EAction::LeftLight3:
    case EAction::LeftLight4: case EAction::LeftLight5: case EAction::LeftLight6:
      return light(size_t(a)-size_t(EAction::LeftLight1));
    case EAction::PairedLight1: case EAction::PairedLight2: case EAction::PairedLight3:
    case EAction::PairedLight4: case EAction::PairedLight5: case EAction::PairedLight6:
      return light(size_t(a)-size_t(EAction::PairedLight1));

    case EAction::Heavy1Charge:
    case EAction::Heavy1: {
      // the last stage of Gothic's combo is the widest, strongest swing
      auto base = segment(pl,sAttack,0,BS_HIT);
      if(base.sq!=nullptr) {
        const size_t segs = std::max<size_t>(1,base.sq->data->defHitEnd.size());
        if(segs>=2)
          return segment(pl,sAttack,segs-1,BS_HIT);
        }
      auto n = firstOf(pl,{tAttackMv},BS_HIT);
      return n.sq ? n : base;
      }
    case EAction::Heavy2Charge:
    case EAction::Heavy2: {
      auto n = firstOf(pl,{tAttackMv,tAttackR},BS_HIT);
      return n.sq ? n : segment(pl,sAttack,0,BS_HIT);
      }
    case EAction::RunLight:
    case EAction::RunHeavy:
    case EAction::RollAttack:
    case EAction::BackstepAttack:
    case EAction::CrouchAttack:
    case EAction::JumpLight:
    case EAction::JumpHeavy:
    case EAction::PairedRun:
    case EAction::PairedRoll:
    case EAction::PairedBackstep:
    case EAction::PairedJump: {
      auto n = firstOf(pl,{tAttackMv},BS_HIT);
      return n.sq ? n : segment(pl,sAttack,0,BS_HIT);
      }
    case EAction::GuardCounter:
      return firstOf(pl,{tAttackL,tAttackR,sAttack},BS_HIT);

    case EAction::Backstep:
      return firstOf(pl,{tJumpB,tJumpB2,"T_JUMPB"},BS_PARADE);
    case EAction::Roll:
    case EAction::CrouchRoll: {
      // Gothic has no roll: sidesteps for lateral dodges, a forward lurch, a jump back
      const float x = up.mMoveX, y = up.mMoveY;
      if(std::abs(x)>=std::abs(y)*0.75f)
        return firstOf(pl,{x<0 ? tStrafeL : tStrafeR, x<0 ? "T_RUNSTRAFEL" : "T_RUNSTRAFER"},BS_RUN);
      if(y<0)
        return firstOf(pl,{tJumpB2,tJumpB,"T_JUMPB"},BS_RUN);
      return firstOf(pl,{"T_STUMBLEB",x<0 ? tStrafeL : tStrafeR},BS_RUN);
      }

    case EAction::GuardHit:
      return firstOf(pl,{tParadeHt,tParade},BS_PARADE);
    case EAction::GuardBreak:
      return firstOf(pl,{"T_STUMBLE"},BS_STUMBLE);

    default:
      return Native();
    }
  }

uint64_t EldenPlayer::mapTime(const Native& n, const EldenCombat::ActionDef& def, float frame) const {
  if(n.hold)
    return n.hit;
  const float from = float(n.from), to = float(n.to);
  const float D    = std::max(def.mDuration,1.f);
  frame = std::clamp(frame,0.f,D);

  float t = from + (to-from)*frame/D;
  if(def.mHitCount>0 && n.hit>n.from && n.hit<n.to) {
    // align Gothic's optimal (contact) frame with the elden hit window
    const auto& h  = def.mHits[0];
    float       hf = h.mFrom + 0.35f*std::max(0.f,h.mTo-h.mFrom);
    hf = std::clamp(hf,0.5f,D-0.5f);
    const float hit = float(n.hit);
    if(frame<=hf)
      t = from + (hit-from)*frame/hf; else
      t = hit  + (to-hit)*(frame-hf)/std::max(D-hf,0.5f);
    }
  t = std::clamp(t,from,std::max(from,to-1.f));
  return uint64_t(t);
  }

void EldenPlayer::stopDrive(Npc& pl, bool toIdle) {
  if(!driving)
    return;
  pl.eldenStopDriven();
  driving   = false;
  drv       = Native();
  drvAction = EAction::Idle;
  if(toIdle)
    pl.eldenForceAnim(pl.weaponState()==WeaponState::NoWeapon ? "S_RUN" : "S_"+prefix(pl),BS_STAND);
  }

void EldenPlayer::driveAnimation(Npc& pl, const EldenCombat::Update& up) {
  const EAction a         = up.mAction;
  const bool   guardHold = (a==EAction::Idle && ctrl.guarding());
  const bool   moving    = std::hypot(moveX,moveY)>0.25f;

  if(!guardHold && isNativeOwned(a)) {
    stopDrive(pl,a==EAction::Idle);
    return;
    }
  if(!guardHold && !up.mMovementLocked && moving && !isDodge(a)) {
    // recovery canceled by movement: Gothic locomotion takes over
    stopDrive(pl,true);
    return;
    }

  const bool restart = !driving || drvAction!=a || drvSeq!=ctrl.sequence() || drv.hold!=guardHold;
  if(restart) {
    Native n;
    if(guardHold) {
      n = firstOf(pl,{"T_"+prefix(pl)+"PARADE_0"},BS_PARADE);
      if(n.sq!=nullptr) {
        // pin the pose in the middle of the parade (its def window when present)
        auto& d = *n.sq->data;
        n.hit  = d.defWindow.size()>=2 ? (d.defWindow[0]+d.defWindow[1])/2 : (n.from+n.to)/2;
        n.hold = true;
        }
      } else {
      n = pickNative(pl,a,up);
      }
    if(n.sq==nullptr || !pl.eldenStartDriven(n.sq,n.bs)) {
      stopDrive(pl,false);
      return;
      }
    drv       = n;
    drvAction = a;
    drvSeq    = ctrl.sequence();
    driving   = true;
    if(debugOn())
      Log::i("elden: action ",int(a)," -> ",n.sq->name," [",n.from,"..",n.to,"] hit=",n.hit,
             " erDuration=",ctrl.definition().mDuration," stamina=",int(stamina));
    }

  const uint64_t t = mapTime(drv,ctrl.definition(),ctrl.frame());
  if(!pl.eldenSetDrivenTime(drv.sq,t,drv.combo)) {
    // layer was replaced by something else (hit reaction, script)
    driving   = false;
    drvAction = EAction::Idle;
    pl.eldenStopDriven();
    }
  }

void EldenPlayer::applyDisplacement(Npc& pl, const EldenCombat::Update& up) {
  if(nativeAnims || isNativeOwned(up.mAction))
    return;
  const float dx = up.mDisplacementX*100.f; // metres -> centimetres
  const float dy = up.mDisplacementY*100.f;
  if(std::abs(dx)<0.001f && std::abs(dy)<0.001f)
    return;
  const float a     = pl.rotationRad();
  const Vec3  fwd   = Vec3(std::cos(a),0,std::sin(a));
  const Vec3  right = Vec3(std::sin(a),0,-std::cos(a));
  const Vec3  dp    = fwd*dy + right*dx;
  if(!pl.tryMove(dp)) {
    // slide along walls
    if(!pl.tryMove(Vec3(dp.x,0,0)))
      pl.tryMove(Vec3(0,0,dp.z));
    }
  }

void EldenPlayer::updateLockOn(Npc& pl, uint64_t dt) {
  if(lock==nullptr)
    return;
  pl.setTarget(lock);
  if(isDodge(ctrl.action()))
    return;
  auto dp = lock->position()-pl.position();
  pl.rotateTo(dp.x,dp.z,540.f,AnimationSolver::TurnType::None,dt);
  }

bool EldenPlayer::dodgesSpell(const Npc& pl) const {
  return ownsMelee(pl) && ctrl.invincible();
  }

bool EldenPlayer::onPlayerHit(Npc& pl, Npc& attacker, const Bullet* b) {
  if(!ownsMelee(pl))
    return true;
  if(ctrl.postHitProtectionRemaining()>0.f)
    return false;

  // rough Gothic hit strength, to size stamina/stance damage
  int32_t power = attacker.attribute(ATR_STRENGTH);
  if(auto w = attacker.inventory().activeWeapon())
    power += w->handle().damage_total;
  else if(attacker.isMonster())
    power += attacker.handle().damage[zenkit::DamageType::EDGE] + attacker.handle().damage[zenkit::DamageType::BLUNT];

  EldenCombat::Incoming in;
  in.mDamage        = float(power);
  in.mStaminaDamage = std::clamp(10.f+float(power)*0.25f,10.f,70.f);
  in.mStanceDamage  = std::clamp(5.f+float(power)*0.15f,5.f,40.f);
  in.mLevel         = power>=120 ? EldenCombat::HurtLevel::Large :
                      power>=60  ? EldenCombat::HurtLevel::Middle : EldenCombat::HurtLevel::Small;
  {
    auto dp = attacker.position()-pl.position();
    dp.y = 0;
    const float len = dp.length();
    const float a   = pl.rotationRad();
    in.mFacingDot = len>0.f ? (dp.x*std::cos(a)+dp.z*std::sin(a))/len : 1.f;
  }
  (void)b;

  const auto res = ctrl.receiveHit(in);
  if(debugOn())
    Log::i("elden: player hit by ",std::string(attacker.displayName())," power=",power," result=",int(res)," stamina=",int(ctrl.stamina()));
  stamina = ctrl.stamina();
  staminaShowTime = 3.f;
  switch(res) {
    case EldenCombat::HitResult::Ignored:
    case EldenCombat::HitResult::Dodged:
    case EldenCombat::HitResult::Jumped:
      return false;
    case EldenCombat::HitResult::Blocked:
    case EldenCombat::HitResult::GuardBroken:
      pl.eldenBlockEffect(attacker);
      return false;
    case EldenCombat::HitResult::Hit:
    case EldenCombat::HitResult::StanceBroken:
      if(res==EldenCombat::HitResult::Hit && !pl.isPlayer() && !isHurt(ctrl.action())) {
        // poise held: Gothic damage applies, but the action (e.g. a swing) goes on unstaggered
        poiseAbsorbed = true;
        ctrl.protectAfterHit(in.mDamage);
        return true;
        }
      // native hit reaction and Gothic damage
      if(driving) {
        pl.eldenStopDriven();
        driving   = false;
        drvAction = EAction::Idle;
        }
      ctrl.protectAfterHit(in.mDamage);
      return true;
    }
  return true;
  }

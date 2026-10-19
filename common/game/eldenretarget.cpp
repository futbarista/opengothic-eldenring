#include "eldenretarget.h"

#include <Tempest/Log>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

#include "eldencombat/animations.hpp"
#include "eldencombat/combat.hpp"
#include "graphics/mesh/animmath.h"
#include "graphics/mesh/skeleton.h"
#include "commandline.h"

using namespace Tempest;

namespace {
constexpr float FadeIn  = 0.15f; // seconds, native -> elden
constexpr size_t NoNode   = size_t(-1);

float dot(const Vec3& a, const Vec3& b) { return Vec3::dotProduct(a,b); }
Vec3  cross(const Vec3& a, const Vec3& b) { return Vec3::crossProduct(a,b); }

Vec3 unit(Vec3 v, const Vec3& fallback) {
  const float l = v.length();
  if(l<1e-5f || !std::isfinite(l))
    return fallback;
  return v/l;
  }

Vec3 perpendicular(const Vec3& axis, Vec3 ref) {
  ref = ref - axis*dot(ref,axis);
  if(ref.quadLength()<1e-10f) {
    ref = std::abs(axis.x)<0.8f ? Vec3(1,0,0) : Vec3(0,1,0);
    ref = ref - axis*dot(ref,axis);
    }
  return unit(ref,Vec3(0,0,1));
  }

// OpenMW EldenPose::bendSecondary: bend-plane normal of a two-segment limb, faded
// towards the anatomical reference while the limb is nearly straight.
Vec3 bendSecondary(const Vec3& upper, const Vec3& lower, const Vec3& primary, const Vec3& referenceNormal) {
  const Vec3 a    = unit(upper,Vec3(0,-1,0));
  const Vec3 b    = unit(lower,a);
  const Vec3 axis = unit(primary,a);
  const Vec3 ref  = perpendicular(axis,referenceNormal);
  Vec3  normal = cross(a,b);
  const float bend = std::clamp(normal.length()/0.15f,0.f,1.f);
  normal = unit(normal,ref);
  if(dot(normal,ref)<0)
    normal = normal*-1.f;
  normal = unit(ref*(1.f-bend)+normal*bend,ref);
  return cross(normal,primary);
  }

Vec3 footSecondary(const Vec3& toe, const Vec3& hipRight) {
  return bendSecondary(toe,cross(hipRight,toe),toe,hipRight);
  }

struct Quat { float x=0,y=0,z=0,w=1; };

Quat slerp(Quat a, Quat b, float t) {
  float c = a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
  if(c<0) { b = {-b.x,-b.y,-b.z,-b.w}; c = -c; }
  float ka = 1.f-t, kb = t;
  if(c<0.9995f) {
    const float th = std::acos(std::clamp(c,-1.f,1.f));
    const float s  = std::sin(th);
    ka = std::sin((1.f-t)*th)/s;
    kb = std::sin(t*th)/s;
    }
  Quat r = {a.x*ka+b.x*kb, a.y*ka+b.y*kb, a.z*ka+b.z*kb, a.w*ka+b.w*kb};
  const float l = std::sqrt(r.x*r.x+r.y*r.y+r.z*r.z+r.w*r.w);
  if(l>0) { r.x/=l; r.y/=l; r.z/=l; r.w/=l; }
  return r;
  }

// rotation part of a Tempest matrix (m[col][row]), row-major 3x3 r[row][col]
Quat quatOf(const Matrix4x4& mt) {
  float r[3][3];
  for(int i=0;i<3;++i)
    for(int j=0;j<3;++j)
      r[i][j] = mt.at(j,i);
  Quat q;
  const float tr = r[0][0]+r[1][1]+r[2][2];
  if(tr>0) {
    float s = std::sqrt(tr+1.f)*2.f;
    q.w = 0.25f*s;
    q.x = (r[2][1]-r[1][2])/s;
    q.y = (r[0][2]-r[2][0])/s;
    q.z = (r[1][0]-r[0][1])/s;
    }
  else if(r[0][0]>r[1][1] && r[0][0]>r[2][2]) {
    float s = std::sqrt(1.f+r[0][0]-r[1][1]-r[2][2])*2.f;
    q.w = (r[2][1]-r[1][2])/s;
    q.x = 0.25f*s;
    q.y = (r[0][1]+r[1][0])/s;
    q.z = (r[0][2]+r[2][0])/s;
    }
  else if(r[1][1]>r[2][2]) {
    float s = std::sqrt(1.f+r[1][1]-r[0][0]-r[2][2])*2.f;
    q.w = (r[0][2]-r[2][0])/s;
    q.x = (r[0][1]+r[1][0])/s;
    q.y = 0.25f*s;
    q.z = (r[1][2]+r[2][1])/s;
    }
  else {
    float s = std::sqrt(1.f+r[2][2]-r[0][0]-r[1][1])*2.f;
    q.w = (r[1][0]-r[0][1])/s;
    q.x = (r[0][2]+r[2][0])/s;
    q.y = (r[1][2]+r[2][1])/s;
    q.z = 0.25f*s;
    }
  return q;
  }

Matrix4x4 matOf(const Quat& q, const Vec3& t) {
  const float x=q.x, y=q.y, z=q.z, w=q.w;
  const float r00 = 1-2*(y*y+z*z), r01 = 2*(x*y-z*w),   r02 = 2*(x*z+y*w);
  const float r10 = 2*(x*y+z*w),   r11 = 1-2*(x*x+z*z), r12 = 2*(y*z-x*w);
  const float r20 = 2*(x*z-y*w),   r21 = 2*(y*z+x*w),   r22 = 1-2*(x*x+y*y);
  return Matrix4x4(r00,r01,r02,t.x,
                   r10,r11,r12,t.y,
                   r20,r21,r22,t.z,
                   0,  0,  0,  1);
  }

Vec3 translationOf(const Matrix4x4& m) {
  return Vec3(m.at(3,0),m.at(3,1),m.at(3,2));
  }

Matrix4x4 blendLocal(const Matrix4x4& a, const Matrix4x4& b, float t) {
  const Vec3 ta = translationOf(a), tb = translationOf(b);
  return matOf(slerp(quatOf(a),quatOf(b),t), ta+(tb-ta)*t);
  }
}

Matrix4x4 EldenRetarget::blend(const Matrix4x4& a, const Matrix4x4& b, float t) {
  return blendLocal(a,b,t);
  }

EldenRetarget::Mat3 EldenRetarget::Mat3::cols(const Vec3& a, const Vec3& b, const Vec3& c) {
  Mat3 r;
  r.m[0][0]=a.x; r.m[0][1]=b.x; r.m[0][2]=c.x;
  r.m[1][0]=a.y; r.m[1][1]=b.y; r.m[1][2]=c.y;
  r.m[2][0]=a.z; r.m[2][1]=b.z; r.m[2][2]=c.z;
  return r;
  }

EldenRetarget::Mat3 EldenRetarget::Mat3::operator*(const Mat3& o) const {
  Mat3 r;
  for(int i=0;i<3;++i)
    for(int j=0;j<3;++j)
      r.m[i][j] = m[i][0]*o.m[0][j]+m[i][1]*o.m[1][j]+m[i][2]*o.m[2][j];
  return r;
  }

Vec3 EldenRetarget::Mat3::operator*(const Vec3& v) const {
  return Vec3(m[0][0]*v.x+m[0][1]*v.y+m[0][2]*v.z,
              m[1][0]*v.x+m[1][1]*v.y+m[1][2]*v.z,
              m[2][0]*v.x+m[2][1]*v.y+m[2][2]*v.z);
  }

EldenRetarget::Mat3 EldenRetarget::Mat3::transposed() const {
  Mat3 r;
  for(int i=0;i<3;++i)
    for(int j=0;j<3;++j)
      r.m[i][j] = m[j][i];
  return r;
  }

EldenRetarget::Mat3 EldenRetarget::rotationOf(const Matrix4x4& mt) {
  // orthonormalize: skeleton matrices may carry a little scale
  Vec3 a = unit(Vec3(mt.at(0,0),mt.at(0,1),mt.at(0,2)),Vec3(1,0,0));
  Vec3 b = Vec3(mt.at(1,0),mt.at(1,1),mt.at(1,2));
  b = unit(b-a*dot(a,b),Vec3(0,1,0));
  Vec3 c = Vec3(mt.at(2,0),mt.at(2,1),mt.at(2,2));
  c = c-a*dot(a,c)-b*dot(b,c);
  c = unit(c,cross(a,b));
  return Mat3::cols(a,b,c);
  }

Matrix4x4 EldenRetarget::compose(const Mat3& r, const Vec3& t) {
  return Matrix4x4(r.m[0][0],r.m[0][1],r.m[0][2],t.x,
                   r.m[1][0],r.m[1][1],r.m[1][2],t.y,
                   r.m[2][0],r.m[2][1],r.m[2][2],t.z,
                   0,0,0,1);
  }

EldenRetarget::Mat3 EldenRetarget::frame(Vec3 primary, Vec3 secondary) {
  primary   = unit(primary,Vec3(0,1,0));
  secondary = secondary - primary*dot(secondary,primary);
  if(secondary.quadLength()<1e-10f) {
    secondary = std::abs(primary.y)<0.8f ? Vec3(0,1,0) : Vec3(0,0,1);
    secondary = secondary - primary*dot(secondary,primary);
    }
  secondary = unit(secondary,Vec3(1,0,0));
  return Mat3::cols(primary,secondary,cross(primary,secondary));
  }

Vec3 EldenRetarget::BendPlane::secondary(const Vec3& upper, const Vec3& lower, const Vec3& primary,
                                         const Vec3& reference, float dt) {
  // OpenMW EldenPose::LimbBendPlane: transport the previous plane along the limb
  // instead of re-choosing its sign every frame (prevents half-turn flips).
  const Vec3 ax = unit(primary,Vec3(0,-1,0));
  Vec3 n;
  if(!ok) {
    n = unit(cross(ax,bendSecondary(upper,lower,primary,reference)),Vec3(1,0,0));
    } else {
    const Vec3  turn = cross(axis,ax);
    const float cs   = std::clamp(dot(axis,ax),-1.f,1.f);
    n = normal;
    if(cs>-0.9999f)
      n = n + cross(turn,normal) + cross(turn,cross(turn,normal))/(1.f+cs);
    n = perpendicular(ax,n);

    const Vec3 a = unit(upper,ax);
    const Vec3 b = unit(lower,a);
    Vec3  bend = cross(a,b);
    const float strength = std::clamp(bend.length()/0.15f,0.f,1.f);
    bend = unit(bend,n);
    if(dot(bend,n)<0)
      bend = bend*-1.f;
    const float amount = dt>0 ? 1.f-std::pow(1.f-strength,dt*30.f) : 0.f;
    n = unit(n*(1.f-amount)+bend*amount,n);
    }
  axis   = ax;
  normal = n;
  ok     = true;
  return cross(n,primary);
  }

std::shared_ptr<const EldenCombat::AnimationBank> EldenRetarget::bank() {
  static std::shared_ptr<const EldenCombat::AnimationBank> ret;
  static bool tried = false;
  if(tried)
    return ret;
  tried = true;

  std::string path = "player_anims.bin";
  if(const char* env = std::getenv("OPENGOTHIC_ELDEN_ANIMS"))
    path = env;
  if(!CommandLine::inst().eldenAnims().empty())
    path = std::string(CommandLine::inst().eldenAnims());
  std::ifstream fin(path,std::ios::binary);
  if(!fin) {
    Log::i("elden: animation bank not found (",path,"), native Gothic animations will be used");
    return ret;
    }
  try {
    ret = std::make_shared<EldenCombat::AnimationBank>(fin);
    Log::i("elden: loaded ",ret->size()," Elden Ring animation clips from ",path);
    }
  catch(const std::exception& e) {
    Log::e("elden: unable to read animation bank ",path,": ",e.what());
    ret.reset();
    }
  return ret;
  }

bool EldenRetarget::isHumanoid(const Skeleton& sk) {
  static std::vector<std::pair<const Skeleton*,bool>> cache;
  for(auto& c:cache)
    if(c.first==&sk)
      return c.second;

  auto decide = [&]() -> std::pair<bool,const char*> {
    static const char* req[] = {
      "BIP01 PELVIS","BIP01 SPINE","BIP01 NECK","BIP01 HEAD",
      "BIP01 L UPPERARM","BIP01 L FOREARM","BIP01 L HAND","BIP01 R UPPERARM","BIP01 R FOREARM","BIP01 R HAND",
      "BIP01 L THIGH","BIP01 L CALF","BIP01 L FOOT","BIP01 R THIGH","BIP01 R CALF","BIP01 R FOOT",
      };
    for(auto r:req)
      if(sk.findNode(r)>=sk.nodes.size())
        return {false,"no biped bones"};
    // rest pose in model space
    std::vector<Matrix4x4> m(sk.nodes.size());
    std::vector<uint8_t>   ok(sk.nodes.size(),0);
    for(size_t pass=0; pass<sk.nodes.size(); ++pass)
      for(size_t i=0; i<sk.nodes.size(); ++i) {
        if(ok[i])
          continue;
        const size_t p = sk.nodes[i].parent;
        if(p<sk.nodes.size() && !ok[p])
          continue;
        m[i] = p<sk.nodes.size() ? m[p] : Matrix4x4::mkIdentity();
        m[i].mul(sk.nodes[i].tr);
        ok[i] = 1;
        }
    auto pos = [&](const char* n) { return translationOf(m[sk.findNode(n)]); };
    const Vec3 pelvis = pos("BIP01 PELVIS"), neck = pos("BIP01 NECK"), head = pos("BIP01 HEAD");
    const Vec3 lf = pos("BIP01 L FOOT"), rf = pos("BIP01 R FOOT");
    const Vec3 torso = neck-pelvis;
    const float len  = torso.length();
    if(len<1.f || torso.y/len<0.7f)
      return {false,"not upright (quadruped)"};
    const float footY = std::min(lf.y,rf.y);
    if(pelvis.y-footY<0.6f*len)
      return {false,"legs not under the pelvis"};
    if(head.y-footY>260.f)
      return {false,"giant"};
    return {true,"humanoid"};
    };
  const auto d = decide();
  cache.emplace_back(&sk,d.first);
  Log::i("elden: skeleton '",std::string(sk.name()),"' -> ",d.first ? "Elden combat" : "native combat"," (",d.second,")");
  return d.first;
  }

bool EldenRetarget::hasClip(std::string_view clip) const {
  return eran!=nullptr && !clip.empty() && eran->find(clip)!=nullptr;
  }

float EldenRetarget::clipLength(std::string_view clip) const {
  if(eran==nullptr || clip.empty())
    return 0;
  auto c = eran->find(clip);
  return c ? float(c->mFrames) : 0.f;
  }

void EldenRetarget::samplePositions(const std::vector<float>& s, float yaw, Joints& out, Vec3& headUp, Vec3& headFwd) const {
  // raw ERAN: +X left, +Y up, +Z forward; turn the pose about +Y towards the right
  const float c = std::cos(yaw), sn = std::sin(yaw);
  auto src = [&](size_t off) {
    const float x = s[off], y = s[off+1], z = s[off+2];
    return toGothic*Vec3(c*x - sn*z, y, sn*x + c*z);
    };
  for(size_t j=0; j<JointCount; ++j)
    out[j] = src(srcOffset[j]);
  if(srcHeadAxes!=NoNode) {
    headUp  = src(srcHeadAxes);
    headFwd = src(srcHeadAxes+6);
    } else {
    auto b  = anatomy(out);
    headUp  = b.up;
    headFwd = b.chestForward;
    }
  }

EldenRetarget::Mat3 EldenRetarget::weaponFrame(const std::vector<float>& s, float yaw) const {
  const float c = std::cos(yaw), sn = std::sin(yaw);
  auto src = [&](size_t off) {
    const float x = s[off], y = s[off+1], z = s[off+2];
    return toGothic*Vec3(c*x - sn*z, y, sn*x + c*z);
    };
  // built like every joint frame, so a reflected source basis cancels in the calibration
  return frame(src(srcWeaponAxes),src(srcWeaponAxes+3));
  }

void EldenRetarget::initWeaponSlot(const Skeleton& sk) {
  weaponSlot    = size_t(-1);
  srcWeaponAxes = eran->axesOffset("R_Weapon").value_or(size_t(-1));
  const size_t slot = sk.findNode("ZS_RIGHTHAND");
  const auto*  s1h  = sk.sequence("S_1H");
  const auto   clip = EldenCombat::stanceAnimation(EldenCombat::WeaponClass::StraightSword,false,0);
  const auto*  er   = eran->find(clip);
  if(srcWeaponAxes==size_t(-1) || slot>=sk.nodes.size() || s1h==nullptr || s1h->data==nullptr || er==nullptr)
    return;

  // Gothic: where its own one-handed stance puts the sword slot (model space, frame 0)
  const auto&  d  = *s1h->data;
  const size_t n  = sk.nodes.size();
  std::vector<Matrix4x4> loc = restLocal, mdl(n);
  if(!d.nodeIndex.empty() && d.samples.size()>=d.nodeIndex.size())
    for(size_t i=0; i<d.nodeIndex.size(); ++i)
      if(d.nodeIndex[i]<n)
        loc[d.nodeIndex[i]] = mkMatrix(d.samples[i]);
  for(size_t i:order) {
    const size_t p = sk.nodes[i].parent;
    mdl[i] = p<n ? mdl[p] : Matrix4x4::mkIdentity();
    mdl[i].mul(loc[i]);
    }
  // Elden Ring: the weapon bone in the matching one-handed stance
  std::vector<float> tmp;
  eran->sample(*er,0.f,false,tmp);
  weaponCorr = weaponFrame(tmp,0.f).transposed()*rotationOf(mdl[slot]);
  weaponSlot = slot;
  maskBones[slot] = 1;
  }

EldenRetarget::Body EldenRetarget::anatomy(const Joints& p) const {
  Body b;
  b.up            = unit(p[Neck]-p[Pelvis],bindUp);
  b.right         = unit(p[RThigh]-p[LThigh],Vec3(1,0,0));
  b.shoulderRight = unit(p[RUpperArm]-p[LUpperArm],b.right);
  b.forward       = unit(cross(b.up,b.right),Vec3(0,0,1));
  b.chestForward  = unit(cross(b.up,b.shoulderRight),b.forward);
  return b;
  }

EldenRetarget::Mat3 EldenRetarget::jointFrame(size_t j, const Joints& p, const Body& b, const Vec3& headUp, const Vec3& headFwd,
                                              std::array<Vec3,10>* pl) const {
  Vec3 primary = b.up, secondary = b.right;
  if(j<=Neck) {
    primary = p[j+1]-p[j];
    // distribute the hip->shoulder twist over the spine
    const float chest = std::min(float(j)/float(Spine2),1.f);
    secondary = b.right*(1.f-chest) + b.shoulderRight*chest;
    }
  else if(j==Head) {
    primary   = headUp;
    secondary = headFwd;
    }
  else if(j>=LClavicle && j<=RHand) {
    const size_t arm  = j<RClavicle ? LClavicle : RClavicle;
    const bool   left = arm==LClavicle;
    if(j==arm) {
      primary   = p[arm+1]-p[arm];
      secondary = b.up;
      }
    else if(j==arm+3) {
      const size_t f1 = left ? LFinger1 : RFinger1, f2 = left ? LFinger2 : RFinger2;
      primary   = p[f2]-p[j];
      secondary = p[f1]-p[f2];
      }
    else {
      primary   = p[j+1]-p[j];
      secondary = pl ? (*pl)[(left ? 0 : 2)+j-arm-1]
                     : bendSecondary(p[arm+2]-p[arm+1],p[arm+3]-p[arm+2],primary,cross(primary,b.chestForward));
      }
    }
  else if(j>=LThigh && j<=RToe) {
    const size_t leg  = j<RThigh ? LThigh : RThigh;
    const bool   left = leg==LThigh;
    if(j>=leg+2) {
      primary   = p[leg+3]-p[leg+2];
      secondary = pl ? (*pl)[left ? 8 : 9] : footSecondary(primary,b.right);
      } else {
      primary   = p[j+1]-p[j];
      secondary = pl ? (*pl)[(left ? 4 : 6)+j-leg]
                     : bendSecondary(p[leg+1]-p[leg],p[leg+2]-p[leg+1],primary,b.right*-1.f);
      }
    }
  return frame(primary,secondary);
  }

bool EldenRetarget::init(const Skeleton& sk) {
  valid = false;
  skel  = &sk;
  eran  = bank();
  if(eran==nullptr)
    return false;

  static const char* srcNames[JointCount] = {
    "Pelvis","Spine","Spine1","Spine2","Neck","Head",
    "L_Clavicle","L_UpperArm","L_Forearm","L_Hand",
    "R_Clavicle","R_UpperArm","R_Forearm","R_Hand",
    "L_Thigh","L_Calf","L_Foot","L_Toe0",
    "R_Thigh","R_Calf","R_Foot","R_Toe0",
    "L_Finger1","L_Finger2","R_Finger1","R_Finger2",
    };
  static const char* dstNames[JointCount] = {
    "BIP01 PELVIS","BIP01 SPINE","BIP01 SPINE1","BIP01 SPINE2","BIP01 NECK","BIP01 HEAD",
    "BIP01 L CLAVICLE","BIP01 L UPPERARM","BIP01 L FOREARM","BIP01 L HAND",
    "BIP01 R CLAVICLE","BIP01 R UPPERARM","BIP01 R FOREARM","BIP01 R HAND",
    "BIP01 L THIGH","BIP01 L CALF","BIP01 L FOOT","BIP01 L TOE0",
    "BIP01 R THIGH","BIP01 R CALF","BIP01 R FOOT","BIP01 R TOE0",
    nullptr,nullptr,nullptr,nullptr,
    };
  for(size_t j=0; j<JointCount; ++j) {
    auto off = eran->jointOffset(srcNames[j]);
    if(!off)
      return false;
    srcOffset[j] = *off;
    }
  srcHeadAxes = eran->axesOffset("Head").value_or(NoNode);

  // rest pose of the Gothic skeleton
  const size_t n = sk.nodes.size();
  restLocal.resize(n);
  restModel.resize(n);
  jointOf.assign(n,JointCount);
  correction.assign(n,Mat3());
  maskBones.assign(n,0);
  order.clear();
  {
    std::vector<uint8_t> done(n,0);
    while(order.size()<n) {
      const size_t before = order.size();
      for(size_t i=0; i<n; ++i) {
        if(done[i])
          continue;
        const size_t p = sk.nodes[i].parent;
        if(p<n && !done[p])
          continue;
        restLocal[i] = sk.nodes[i].tr;
        if(p<n) {
          restModel[i] = restModel[p];
          restModel[i].mul(restLocal[i]);
          } else {
          restModel[i] = restLocal[i];
          }
        done[i] = 1;
        order.push_back(i);
        }
      if(order.size()==before)
        return false; // cycle?
      }
  }

  node.fill(NoNode);
  for(size_t j=0; j<JointCount; ++j) {
    if(dstNames[j]==nullptr)
      continue;
    node[j] = sk.findNode(dstNames[j]);
    if(node[j]>=n) {
      if(j==LToe || j==RToe)
        continue; // optional
      Log::i("elden: skeleton '",std::string(sk.name()),"' has no ",dstNames[j],", retargeting disabled");
      return false;
      }
    jointOf[node[j]] = j;
    }
  // every ancestor of a retargeted bone is pinned to its rest pose
  for(size_t j=0; j<JointCount; ++j) {
    for(size_t i=node[j]; i<n; i=sk.nodes[i].parent)
      maskBones[i] = 1;
    }

  Joints bind{};
  for(size_t j=0; j<JointCount; ++j)
    if(node[j]<n)
      bind[j] = translationOf(restModel[node[j]]);

  // source reference: neutral standing pose
  const auto* standing = eran->find("a000_000000");
  if(standing==nullptr)
    standing = eran->find("a000_020000");
  if(standing==nullptr)
    return false;
  eran->sample(*standing,0.f,false,smp);
  auto raw = [&](size_t j) { return Vec3(smp[srcOffset[j]],smp[srcOffset[j]+1],smp[srcOffset[j]+2]); };

  // basis change from anatomy, not from assumed axis conventions
  auto basis = [](Vec3 up, Vec3 right, Vec3 fwd) {
    up    = unit(up,Vec3(0,1,0));
    right = unit(right-up*dot(right,up),Vec3(1,0,0));
    Vec3 f = fwd-up*dot(fwd,up)-right*dot(fwd,right);
    f = unit(f,cross(up,right));
    return Mat3::cols(right,up,f);
    };
  const Vec3 srcToe = (raw(LToe)-raw(LFoot)) + (raw(RToe)-raw(RFoot));
  const Mat3 bs = basis(raw(Neck)-raw(Pelvis), raw(RThigh)-raw(LThigh), srcToe);
  Vec3 dstToe;
  if(node[LToe]<n && node[RToe]<n)
    dstToe = (bind[LToe]-bind[LFoot]) + (bind[RToe]-bind[RFoot]);
  else
    dstToe = cross(bind[Neck]-bind[Pelvis], bind[RThigh]-bind[LThigh]); // guess
  const Mat3 bd = basis(bind[Neck]-bind[Pelvis], bind[RThigh]-bind[LThigh], dstToe);
  toGothic = bd*bs.transposed();
  bindUp   = unit(bind[Neck]-bind[Pelvis],Vec3(0,1,0));

  // virtual bind points for joints Gothic does not have
  for(size_t j:{LToe,RToe})
    if(node[j]>=n)
      bind[j] = bind[j-1] + bd*Vec3(0,0,10);

  // calibration against the Gothic rest pose
  const Body bindBody = anatomy(bind);
  for(size_t j=0; j<JointCount; ++j) {
    if(node[j]>=n)
      continue;
    const Mat3 rest = rotationOf(restModel[node[j]]);
    correction[node[j]] = jointFrame(j,bind,bindBody,bindBody.up,bindBody.chestForward,nullptr).transposed()*rest;
    }

  // hands, feet, toes and head carry no reliable bind direction (no fingers, flat soles):
  // calibrate them relative to their parent on the source's neutral standing pose
  Joints ref;
  Vec3   refHeadUp, refHeadFwd;
  samplePositions(smp,0.f,ref,refHeadUp,refHeadFwd);
  const Body refBody = anatomy(ref);
  auto refModel = [&](size_t j) {
    return jointFrame(j,ref,refBody,refHeadUp,refHeadFwd,nullptr)*correction[node[j]];
    };
  const std::pair<size_t,size_t> relative[] = {
    {Head,Neck},{LHand,LForearm},{RHand,RForearm},{LFoot,LCalf},{RFoot,RCalf},{LToe,LFoot},{RToe,RFoot}
    };
  for(auto [j,parent]:relative) {
    if(node[j]>=n || node[parent]>=n)
      continue;
    // rest orientation of j relative to its anatomical parent, in the Gothic skeleton
    const Mat3 restRel = rotationOf(restModel[node[parent]]).transposed()*rotationOf(restModel[node[j]]);
    const Mat3 target  = refModel(parent)*restRel;
    correction[node[j]] = jointFrame(j,ref,refBody,refHeadUp,refHeadFwd,nullptr).transposed()*target;
    }

  const float srcLeg = (ref[LThigh]-ref[LCalf]).length() + (ref[LCalf]-ref[LFoot]).length();
  const float dstLeg = (bind[LThigh]-bind[LCalf]).length() + (bind[LCalf]-bind[LFoot]).length();
  scale           = srcLeg>1e-5f ? dstLeg/srcLeg : 100.f;
  srcPelvisHeight = dot(ref[Pelvis],bindUp);

  // weapon-bone alignment (initWeaponSlot) is disabled: Gothic's hand slot already holds the
  // weapon correctly under the retargeted hand, the calibration flipped the grip

  // two-handed slot offset relative to the hand, from Gothic's own two-handed stance
  handSlot = sk.findNode("ZS_RIGHTHAND");
  if(handSlot<n) {
    slot2HLocal = restLocal[handSlot];
    if(auto s2h = sk.sequence("S_2H"); s2h!=nullptr && s2h->data!=nullptr) {
      auto& d = *s2h->data;
      for(size_t i=0; i<d.nodeIndex.size() && i<d.samples.size(); ++i)
        if(d.nodeIndex[i]==handSlot)
          slot2HLocal = mkMatrix(d.samples[i]);
      }
    }

  targetLocal = restLocal;
  fromLocal   = restLocal;
  shownLocal  = restLocal;
  model.resize(n);
  reset();
  valid = true;
  static bool logged = false;
  if(!logged) {
    logged = true;
    Log::i("elden: retargeting ",eran->size()," clips onto '",std::string(sk.name()),"' (scale ",scale,")");
    }
  return true;
  }

void EldenRetarget::reset() {
  for(auto& p:planes)
    p.ok = false;
  curClip.clear();
  curFrame  = 0;
  fade      = 1;
  fadeLen   = 0;
  wOverride = 0;
  }

void EldenRetarget::release(float dt) {
  if(wOverride<=0)
    return;
  wOverride = std::max(0.f,wOverride-dt/FadeIn);
  if(wOverride<=0) {
    curClip.clear();
    for(auto& p:planes)
      p.ok = false;
    }
  }

bool EldenRetarget::play(std::string_view clipName, float frame, bool loop, float dt, float yaw) {
  if(!valid || !std::isfinite(frame) || !std::isfinite(yaw))
    return false;
  const auto* clip = eran->find(clipName);
  if(clip==nullptr)
    return false;
  eran->sample(*clip,frame,loop,smp);

  const bool restarted = !loop && frame+0.01f<curFrame;
  if(curClip!=clipName || restarted) {
    // crossfade from what is shown now
    fromLocal = (wOverride>0) ? shownLocal : targetLocal;
    fade      = 0;
    fadeLen   = wOverride>0 ? clip->mBlendFrames/30.f : 0.f;
    curClip   = std::string(clipName);
    for(auto& p:planes)
      p.ok = false;
    }
  curFrame = frame;

  Joints p;
  Vec3   headUp, headFwd;
  samplePositions(smp,yaw,p,headUp,headFwd);
  const Body body = anatomy(p);

  std::array<Vec3,10> pl;
  for(size_t i=0; i<4; ++i) {
    const size_t arm = i<2 ? LClavicle : RClavicle;
    const Vec3 upper = p[arm+2]-p[arm+1];
    const Vec3 lower = p[arm+3]-p[arm+2];
    const Vec3 prim  = (i%2==0) ? upper : lower;
    pl[i] = planes[i].secondary(upper,lower,prim,cross(prim,body.chestForward),dt);
    }
  for(size_t i=0; i<4; ++i) {
    const size_t leg = i<2 ? LThigh : RThigh;
    const Vec3 upper = p[leg+1]-p[leg];
    const Vec3 lower = p[leg+2]-p[leg+1];
    const Vec3 prim  = (i%2==0) ? upper : lower;
    pl[i+4] = planes[i+4].secondary(upper,lower,prim,body.right*-1.f,dt);
    }
  for(size_t i=0; i<2; ++i) {
    const size_t foot = i==0 ? LFoot : RFoot;
    const Vec3 prim = p[foot+1]-p[foot];
    pl[i+8] = planes[i+8].secondary(prim,cross(body.right,prim),prim,body.right,dt);
    }

  const size_t n = restLocal.size();
  for(size_t i:order) {
    const size_t parent = skel->nodes[i].parent;
    const Matrix4x4 parentModel = parent<n ? model[parent] : Matrix4x4::mkIdentity();
    const size_t j = jointOf[i];
    if(i==handSlot && slot2H) {
      targetLocal[i] = slot2HLocal;
      }
    else if(i==weaponSlot) {
      // blade follows Elden Ring's weapon bone, the grip stays where Gothic puts it in the hand
      const Mat3 want      = weaponFrame(smp,yaw)*weaponCorr;
      const Mat3 parentRot = parent<n ? rotationOf(parentModel) : Mat3();
      targetLocal[i] = compose(parentRot.transposed()*want,translationOf(restLocal[i]));
      }
    else if(j==JointCount) {
      targetLocal[i] = restLocal[i];
      } else {
      const Mat3 want      = jointFrame(j,p,body,headUp,headFwd,&pl)*correction[i];
      const Mat3 parentRot = parent<n ? rotationOf(parentModel) : Mat3();
      const Mat3 localRot  = parentRot.transposed()*want;
      Vec3 t = translationOf(restLocal[i]);
      if(j==Pelvis) {
        // keep the rest pelvis position, follow the source's crouch/lift
        Vec3 modelT = translationOf(restModel[i]) + bindUp*((dot(p[Pelvis],bindUp)-srcPelvisHeight)*scale);
        Matrix4x4 inv = parentModel;
        inv.inverse();
        const Vec4 lt = inv*Vec4(modelT.x,modelT.y,modelT.z,1);
        t = Vec3(lt.x,lt.y,lt.z);
        }
      targetLocal[i] = compose(localRot,t);
      }
    model[i] = parentModel;
    model[i].mul(targetLocal[i]);
    }

  if(handSlot<n)
    maskBones[handSlot] = slot2H ? 1 : 0;

  // crossfade between Elden clips, fade in over the native pose
  fade += dt;
  float b = fadeLen>0 ? std::clamp(fade/fadeLen,0.f,1.f) : 1.f;
  b = b*b*(3.f-2.f*b);
  for(size_t i=0; i<n; ++i) {
    if(!maskBones[i])
      continue;
    shownLocal[i] = b>=1.f ? targetLocal[i] : blendLocal(fromLocal[i],targetLocal[i],b);
    }
  // never hand a broken matrix to the skinning/attachment code
  for(size_t i=0; i<n; ++i) {
    if(!maskBones[i])
      continue;
    for(int c=0;c<4;++c)
      for(int r=0;r<4;++r)
        if(!std::isfinite(shownLocal[i].at(c,r)) || std::abs(shownLocal[i].at(c,r))>1e5f) {
          Log::e("elden: invalid retargeted matrix for bone ",skel->nodes[i].name," in clip ",curClip,", disabling pose");
          shownLocal = restLocal;
          wOverride  = 0;
          return false;
          }
    }
  wOverride = std::min(1.f,wOverride+dt/FadeIn);
  return true;
  }

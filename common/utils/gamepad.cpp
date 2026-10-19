#include "gamepad.h"

#include <cmath>
#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {
struct XInputGamepad {
  WORD  wButtons;
  BYTE  bLeftTrigger;
  BYTE  bRightTrigger;
  SHORT sThumbLX;
  SHORT sThumbLY;
  SHORT sThumbRX;
  SHORT sThumbRY;
  };
struct XInputState {
  DWORD         dwPacketNumber;
  XInputGamepad Gamepad;
  };
using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XInputState*);
}
#endif

static float stickAxis(int v) {
  float f = float(v)/32767.f;
  f = std::clamp(f,-1.f,1.f);
  return f;
  }

static void applyRadialDeadzone(float& x, float& y, float deadzone) {
  const float len = std::sqrt(x*x+y*y);
  if(len<deadzone) {
    x = 0;
    y = 0;
    return;
    }
  const float k = std::min(1.f,(len-deadzone)/(1.f-deadzone))/len;
  x *= k;
  y *= k;
  }

Gamepad& Gamepad::inst() {
  static Gamepad pad;
  return pad;
  }

Gamepad::Gamepad() {
#if defined(_WIN32)
  const wchar_t* dlls[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
  for(auto name:dlls) {
    if(auto m = LoadLibraryW(name)) {
      module   = m;
      getState = reinterpret_cast<void*>(GetProcAddress(m,"XInputGetState"));
      if(getState!=nullptr)
        break;
      }
    }
#endif
  }

bool Gamepad::isActive() const {
  if(!cur.connected)
    return false;
  return cur.buttons!=0 || cur.lx!=0 || cur.ly!=0 || cur.rx!=0 || cur.ry!=0;
  }

void Gamepad::poll() {
  prev = cur;
  cur  = State();
#if defined(_WIN32)
  if(getState==nullptr)
    return;
  auto fn = reinterpret_cast<XInputGetStateFn>(getState);

  // probing disconnected pads is slow, so only rescan from time to time
  XInputState st = {};
  bool ok = (fn(padIndex,&st)==ERROR_SUCCESS);
  if(!ok) {
    if(retryTick>0) {
      --retryTick;
      return;
      }
    retryTick = 120;
    for(DWORD i=0; i<4; ++i) {
      if(fn(i,&st)==ERROR_SUCCESS) {
        padIndex = i;
        ok       = true;
        break;
        }
      }
    if(!ok)
      return;
    }

  auto& g = st.Gamepad;
  cur.connected = true;
  cur.buttons   = g.wButtons;
  cur.lx = stickAxis(g.sThumbLX);
  cur.ly = stickAxis(g.sThumbLY);
  cur.rx = stickAxis(g.sThumbRX);
  cur.ry = stickAxis(g.sThumbRY);
  applyRadialDeadzone(cur.lx,cur.ly,0.24f);
  applyRadialDeadzone(cur.rx,cur.ry,0.20f);
  cur.lt = float(g.bLeftTrigger) /255.f;
  cur.rt = float(g.bRightTrigger)/255.f;
  if(cur.lt>0.35f)
    cur.buttons |= LT;
  if(cur.rt>0.35f)
    cur.buttons |= RT;
#endif
  }

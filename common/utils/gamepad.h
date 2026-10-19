#pragma once

#include <cstdint>

// Minimal XInput gamepad reader (Windows). On other platforms it reports no pad.
class Gamepad final {
  public:
    enum Button : uint32_t {
      DPadUp    = 0x0001,
      DPadDown  = 0x0002,
      DPadLeft  = 0x0004,
      DPadRight = 0x0008,
      Start     = 0x0010,
      Back      = 0x0020,
      LThumb    = 0x0040,
      RThumb    = 0x0080,
      LB        = 0x0100,
      RB        = 0x0200,
      A         = 0x1000,
      B         = 0x2000,
      X         = 0x4000,
      Y         = 0x8000,
      // virtual buttons for analog triggers
      LT        = 0x10000,
      RT        = 0x20000,
      };

    struct State {
      bool     connected = false;
      uint32_t buttons   = 0;
      float    lx = 0, ly = 0; // left stick, -1..1, deadzone applied
      float    rx = 0, ry = 0; // right stick
      float    lt = 0, rt = 0; // triggers 0..1
      };

    static Gamepad& inst();

    // Polls the first connected pad; call once per frame.
    void         poll();
    const State& state() const { return cur; }
    bool         held    (Button b) const { return (cur.buttons & b)!=0; }
    bool         pressed (Button b) const { return (cur.buttons & b)!=0 && (prev.buttons & b)==0; }
    bool         released(Button b) const { return (cur.buttons & b)==0 && (prev.buttons & b)!=0; }
    bool         isConnected() const { return cur.connected; }
    // True if the pad was touched since the last poll (sticks or buttons)
    bool         isActive() const;

  private:
    Gamepad();

    State    cur, prev;
    void*    module   = nullptr;
    void*    getState = nullptr;
    uint32_t padIndex = 0;
    uint32_t retryTick = 0;
  };

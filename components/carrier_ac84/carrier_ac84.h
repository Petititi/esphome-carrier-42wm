// CARRIER_AC84 IR protocol encoder / decoder (no ESPHome dependency).
// Carrier 42WM100CT fan coil, remote 3021203 RR03-S. See docs/ir-protocol.md.
//
// State: 11 bytes, same layout as IRremoteESP8266 >= 2.8.5
//   s[0]    4 bits only, power: 0xC = on, 0x3 = off
//   s[1]    low nibble = mode (0 auto, 2 fan, 3 heat, 4 cool)
//           high nibble = fan speed (0 auto, 1..3)
//   s[2]    high nibble = setpoint units
//           low nibble: bit 3 timer, bit 2 swing, bits 1-0 always 1
//   s[3]    low nibble = setpoint tens, high nibble = 3
//   s[4..7] timer start / end (minutes, hours, BCD)
//   s[8..9] current time (minutes, hours, BCD)
//   s[10]   checksum = (s[0] + sum(swap_nibbles(s[1..9]))) & 0xFF
//
// Raw durations use the ESPHome convention: mark > 0, space < 0, in microseconds.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace carrier84 {

static const size_t STATE_LEN = 11;
static const int32_t HDR_MARK = 5850;
static const int32_t T_LONG = 1175;
static const int32_t T_SHORT = 430;

// Real frame captured from the remote: on, fan only, low speed, 30 degC, 14:12.
// Used as a template: only known fields are changed, unknown bits stay as the
// remote sent them.
static const uint8_t DEFAULT_STATE[STATE_LEN] = {
    0x0C, 0x12, 0x03, 0x33, 0x00, 0x12, 0x00, 0x12, 0x12, 0x14, 0x34};

// ---------------------------------------------------------------- checksum --
inline uint8_t swap_nibbles(uint8_t x) { return (uint8_t) ((x << 4) | (x >> 4)); }

inline uint8_t checksum(const uint8_t *s) {
  unsigned sum = s[0] & 0x0F;
  for (int i = 1; i < 10; i++) sum += swap_nibbles(s[i]);
  return (uint8_t) (sum & 0xFF);
}
inline bool checksum_ok(const uint8_t *s) { return s[10] == checksum(s); }
inline void fix_checksum(uint8_t *s) { s[10] = checksum(s); }

// ------------------------------------------------------------------ fields --
static const uint8_t POWER_ON = 0xC;
static const uint8_t POWER_OFF = 0x3;
inline bool get_power(const uint8_t *s) { return (s[0] & 0x0F) == POWER_ON; }
inline void set_power(uint8_t *s, bool on) { s[0] = on ? POWER_ON : POWER_OFF; }

/// Setpoint in degC, or -1 if the digits are not valid.
inline int get_temp(const uint8_t *s) {
  int units = s[2] >> 4, tens = s[3] & 0x0F;
  if (units > 9 || tens > 9) return -1;
  return tens * 10 + units;
}
inline void set_temp(uint8_t *s, int t) {
  if (t < 0) t = 0;
  if (t > 99) t = 99;
  s[2] = (uint8_t) ((s[2] & 0x0F) | ((t % 10) << 4));
  s[3] = (uint8_t) ((s[3] & 0xF0) | (t / 10));
}

static const uint8_t MODE_AUTO = 0x0;
static const uint8_t MODE_FAN = 0x2;
static const uint8_t MODE_HEAT = 0x3;
static const uint8_t MODE_COOL = 0x4;
inline uint8_t get_mode(const uint8_t *s) { return s[1] & 0x0F; }
inline void set_mode(uint8_t *s, uint8_t m) { s[1] = (uint8_t) ((s[1] & 0xF0) | (m & 0x0F)); }
inline uint8_t get_fan(const uint8_t *s) { return s[1] >> 4; }  // 0 = auto, 1..3
inline void set_fan(uint8_t *s, uint8_t f) { s[1] = (uint8_t) ((s[1] & 0x0F) | (f << 4)); }

inline const char *mode_name(uint8_t m) {
  switch (m) {
    case MODE_AUTO: return "auto";
    case MODE_FAN: return "fan";
    case MODE_HEAT: return "heat";
    case MODE_COOL: return "cool";
    default: return "?";
  }
}
inline const char *fan_name(uint8_t f) {
  switch (f) {
    case 0: return "auto";
    case 1: return "1";
    case 2: return "2";
    case 3: return "3";
    default: return "?";
  }
}

// Byte 2, low nibble.
static const uint8_t FLAG_TIMER = 0x08;
static const uint8_t FLAG_SWING = 0x04;
inline bool get_timer(const uint8_t *s) { return s[2] & FLAG_TIMER; }
inline bool get_swing(const uint8_t *s) { return s[2] & FLAG_SWING; }
inline void set_swing(uint8_t *s, bool on) {
  s[2] = on ? (uint8_t) (s[2] | FLAG_SWING) : (uint8_t) (s[2] & ~FLAG_SWING);
}

inline uint8_t to_bcd(int v) { return (uint8_t) (((v / 10) << 4) | (v % 10)); }
inline int from_bcd(uint8_t v) {
  if ((v >> 4) > 9 || (v & 0x0F) > 9) return -1;
  return (v >> 4) * 10 + (v & 0x0F);
}

/// Current time (byte 8 = minutes, byte 9 = hours).
inline void set_clock(uint8_t *s, int hour, int minute) {
  s[8] = to_bcd(minute);
  s[9] = to_bcd(hour);
}
inline int get_clock_hour(const uint8_t *s) { return from_bcd(s[9]); }
inline int get_clock_minute(const uint8_t *s) { return from_bcd(s[8]); }

// ----------------------------------------------------------------- text ----
inline std::string to_hex(const uint8_t *s) {
  char buf[STATE_LEN * 3 + 1];
  for (size_t i = 0; i < STATE_LEN; i++) snprintf(buf + i * 3, 4, "%02X ", s[i]);
  buf[STATE_LEN * 3 - 1] = '\0';
  return std::string(buf);
}

// --------------------------------------------------------------- encoding ---
inline void append_bit(std::vector<int32_t> &v, bool bit) {
  // Constant bit time: 1 = long mark + short space, 0 = the opposite.
  if (bit) {
    v.push_back(T_LONG);
    v.push_back(-T_SHORT);
  } else {
    v.push_back(T_SHORT);
    v.push_back(-T_LONG);
  }
}

/// 11 bytes -> 171 durations (header, 4 + 80 bits LSB first, trailing mark).
inline std::vector<int32_t> encode(const uint8_t *s) {
  std::vector<int32_t> v;
  v.reserve(171);
  v.push_back(HDR_MARK);
  v.push_back(-T_LONG);
  for (int i = 0; i < 4; i++) append_bit(v, (s[0] >> i) & 1);
  for (size_t b = 1; b < STATE_LEN; b++)
    for (int i = 0; i < 8; i++) append_bit(v, (s[b] >> i) & 1);
  v.push_back(T_LONG);
  return v;
}

// --------------------------------------------------------------- decoding ---
/// Looks for a frame in received durations. Returns true and fills `out` when
/// a complete frame is found (the checksum is checked separately). Each bit is
/// decided by comparing its mark and its space, not by absolute durations.
inline bool decode(const std::vector<int32_t> &raw, uint8_t *out) {
  const size_t n = raw.size();
  for (size_t start = 0; start + 2 + 84 * 2 <= n; start++) {
    const int32_t hm = raw[start], hs = -raw[start + 1];
    if (hm < 4500 || hm > 7500 || hs < 700 || hs > 1700) continue;
    uint8_t s[STATE_LEN] = {0};
    bool ok = true;
    size_t idx = start + 2;
    for (int bit = 0; bit < 84 && ok; bit++, idx += 2) {
      const int32_t mark = raw[idx], space = -raw[idx + 1];
      if (mark <= 0 || space <= 0) { ok = false; break; }
      const int32_t period = mark + space;
      if (period < 1100 || period > 2300) { ok = false; break; }
      const bool one = mark > space;
      if (bit < 4) {
        if (one) s[0] |= (uint8_t) (1 << bit);
      } else {
        const int b = (bit - 4) / 8 + 1, i = (bit - 4) % 8;
        if (one) s[b] |= (uint8_t) (1 << i);
      }
    }
    if (!ok) continue;
    for (size_t i = 0; i < STATE_LEN; i++) out[i] = s[i];
    return true;
  }
  return false;
}

}  // namespace carrier84

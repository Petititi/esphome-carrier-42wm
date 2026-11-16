# CARRIER_AC84 IR protocol

Remote: **Carrier 3021203 RR03-S**, appliance: **Carrier 42WM100CTG-A**.

The frame format was first captured in
[IRremoteESP8266 issue #1943](https://github.com/crankyoldgit/IRremoteESP8266/issues/1943)
and has been supported at the bit level (`sendCarrierAC84()` / `decodeCarrierAC84()`)
since IRremoteESP8266 v2.8.5
([PR #1945](https://github.com/crankyoldgit/IRremoteESP8266/pull/1945)).
That library does not interpret the fields; this document does.

## Physical layer

Constant bit time: each bit is one mark and one space, about 1600 µs in total.

| Element | Mark (µs) | Space (µs) |
|---|---|---|
| Header | 5850 | 1175 |
| Bit `1` | 1175 | 430 |
| Bit `0` | 430 | 1175 |
| Footer | 1175 | idle |

A frame is the header, 4 bits, then 10 bytes (80 bits), **LSB first**: 171 durations.

Over the air the signal is modulated at 38 kHz. On the wire between the front
panel and the main board the receiver has already removed the carrier, hence
`carrier_duty_percent: 100%` in the ESPHome configuration.

## Frame layout

Same representation as IRremoteESP8266: `state[11]`, where byte 0 only holds
the first 4 bits.

| Byte | Content | Status |
|---|---|---|
| 0 (4 bits) | Power: `0xC` on, `0x3` off | Verified |
| 1, low nibble | Mode: `0` auto, `2` fan only, `3` heat, `4` cool | Verified |
| 1, high nibble | Fan speed: `0` auto, `1`, `2`, `3` | 1–3 verified by transmission; auto not captured |
| 2, high nibble | Setpoint, units digit | Verified |
| 2, low nibble | bit 3 timer, bit 2 swing, bits 1–0 always `1` | Swing verified; timer not captured |
| 3 | Low nibble: setpoint, tens digit. High nibble: always `3` | Verified |
| 4 / 5 | Timer start: minutes / hours, BCD | Likely |
| 6 / 7 | Timer end: minutes / hours, BCD | Likely |
| 8 / 9 | Remote clock: minutes / hours, BCD | Likely |
| 10 | Checksum | Verified |

The remote never sends "warmer" or "faster": every key press sends the whole
desired state. A lost frame therefore shifts nothing; the next one puts
everything right.

### Checksum

```
checksum = (state[0] + Σ swap_nibbles(state[i]) for i in 1..9) & 0xFF
```

Checked on every frame below and on the 21 setpoint captures (10 → 30 °C) of
issue #1943. [`tools/test_ac84.py`](../tools/test_ac84.py) replays them.

### Example

On, heat, low speed, 30 °C, sent at 14:13:

```
0C 13 03 33 00 12 00 12 13 14 54
```

`C`: on. `13`: fan speed 1, mode 3 (heat). `03` then `33`: units 0, tens 3 →
30 °C. `13 14`: 14:13. `54`: checksum.

## Captures

The remote is set up first, then "send" is pressed.

| Frame | Setting |
|---|---|
| `03 12 03 33 00 12 00 12 11 14 1B` | off, fan only, speed 1, 30 °C |
| `0C 12 03 33 00 12 00 12 12 14 34` | on, fan only, speed 1 |
| `0C 13 03 33 00 12 00 12 13 14 54` | on, heat, speed 1 |
| `0C 14 03 33 00 12 00 12 13 14 64` | on, cool, speed 1 |
| `0C 13 07 33 00 12 00 12 14 14 A4` | on, heat, swing, speed 1 |
| `0C 13 03 33 00 12 00 12 15 14 74` | on, heat, no swing |
| `03 13 03 33 00 12 00 12 15 14 6B` | off (otherwise as above) |

Verified by transmission on the appliance: on, off, fan speeds 1 → 2 → 3, swing.
The appliance acknowledges each frame with a beep.

## The six constant bits

Six bits never change in captures: byte 2 bits 0 and 1 (always 1), byte 3
bits 4 and 5 (always 1) and bits 6 and 7 (always 0). The learning firmware can
flip them one at a time, mainly to look for a way to silence the beep.

| Bit flipped | Result |
|---|---|
| byte 2 bit 0, byte 3 bits 4–7 | No visible effect, command executed, same beep |
| byte 2 bit 1 | **Frame rejected** |

There is no "silent" bit in the protocol. The production component always
sends these six bits with their usual value.

## Not explored

- Fan speed "auto" and the timer were never captured from the remote.
- The appliance may or may not use the clock bytes; the component sends the
  real time (SNTP), as the remote does.

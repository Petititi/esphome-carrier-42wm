#!/usr/bin/env python3
"""Checks the CARRIER_AC84 codec against real captures.

Python port of components/carrier_ac84/carrier_ac84.h. Run: python3 tools/test_ac84.py
"""
import random

HDR_MARK, T_LONG, T_SHORT = 5850, 1175, 430


def swap_nibbles(x):
    return ((x << 4) | (x >> 4)) & 0xFF


def checksum(s):
    return ((s[0] & 0x0F) + sum(swap_nibbles(x) for x in s[1:10])) & 0xFF


def encode(s):
    bits = [(s[0] >> i) & 1 for i in range(4)]
    bits += [(s[b] >> i) & 1 for b in range(1, 11) for i in range(8)]
    out = [HDR_MARK, -T_LONG]
    for bit in bits:
        out += [T_LONG, -T_SHORT] if bit else [T_SHORT, -T_LONG]
    return out + [T_LONG]


def decode(raw):
    for start in range(len(raw) - 170 + 1):
        if not (4500 <= raw[start] <= 7500 and 700 <= -raw[start + 1] <= 1700):
            continue
        s, idx = [0] * 11, start + 2
        for bit in range(84):
            mark, space = raw[idx], -raw[idx + 1]
            idx += 2
            if mark <= 0 or space <= 0 or not 1100 <= mark + space <= 2300:
                break
            if mark > space:
                if bit < 4:
                    s[0] |= 1 << bit
                else:
                    s[(bit - 4) // 8 + 1] |= 1 << ((bit - 4) % 8)
        else:
            return s
    return None


def setpoint(s):
    return (s[3] & 0x0F) * 10 + (s[2] >> 4)


def frame(text):
    return [int(x, 16) for x in text.split()]


# Raw capture from IRremoteESP8266 issue #1943 (unsigned, mark first).
RAW = [5844, 1152,
       1154, 462, 1152, 462, 418, 1194, 418, 1194, 420, 1194, 416, 1198,
       418, 1196, 416, 1196, 418, 1194, 420, 1192, 420, 1194, 420, 1194,
       1152, 460, 1152, 460, 420, 1192, 418, 1194, 1154, 458, 418, 1194,
       420, 1192, 1154, 460, 1152, 458, 420, 1194, 418, 1194, 418, 1194,
       1152, 460, 1154, 458, 418, 1196, 416, 1194, 418, 1194, 418, 1196,
       420, 1194, 418, 1192, 420, 1194, 420, 1192, 418, 1196, 416, 1194,
       416, 1196, 1152, 462, 416, 1194, 422, 1192, 1154, 458, 420, 1192,
       418, 1194, 418, 1194, 418, 1196, 420, 1194, 418, 1192, 420, 1194,
       418, 1192, 420, 1194, 418, 1194, 420, 1194, 418, 1194, 1154, 458,
       418, 1194, 418, 1192, 1154, 458, 418, 1196, 416, 1194, 420, 1194,
       416, 1192, 418, 1194, 1152, 462, 416, 1196, 1152, 458, 418, 1196,
       1152, 458, 418, 1194, 1154, 460, 418, 1194, 418, 1196, 418, 1196,
       418, 1194, 1154, 460, 420, 1192, 418, 1194, 420, 1194, 418, 1194,
       418, 1196, 1152, 460, 418, 1194, 1154, 458, 1154, 460, 1152, 458,
       1152]
RAW_STATE = frame("03 00 93 31 00 12 00 12 54 21 E8")  # off, 19 degC, 21:54

# Frames captured from the remote (docs/ir-protocol.md).
CAPTURES = [
    "03 12 03 33 00 12 00 12 11 14 1B",
    "0C 12 03 33 00 12 00 12 12 14 34",
    "0C 13 03 33 00 12 00 12 13 14 54",
    "0C 14 03 33 00 12 00 12 13 14 64",
    "0C 13 07 33 00 12 00 12 14 14 A4",
    "0C 13 03 33 00 12 00 12 15 14 74",
    "03 13 03 33 00 12 00 12 15 14 6B",
    "03 00 03 32 00 12 00 12 33 11 DC",
]

# Setpoints 10..30 degC from issue #1943, in its original MSB-first notation.
SETPOINTS_MSB = {
    10: "0C00C08C004800488498D4", 11: "0C00C88C00480048849834", 12: "0C00C48C004800488498B4",
    13: "0C00CC8C00480048849874", 14: "0C00C28C004800488498F4", 15: "0C00CA8C0048004884980C",
    16: "0C00C68C0048004884988C", 17: "0C00CE8C0048004884984C", 18: "0C00C18C004800488498CC",
    19: "0C00C98C0048004884982C", 20: "0C00C04C004800488498DC", 21: "0C00C84C0048004884983C",
    22: "0C00C44C004800488498BC", 23: "0C00CC4C0048004884987C", 24: "0C00C24C004800488498FC",
    25: "0C00CA4C00480048849802", 26: "0C00C64C00480048849882", 27: "0C00CE4C00480048849842",
    28: "0C00C14C004800488498C2", 29: "0C00C94C00480048849822", 30: "0C00C0CC004800488498D2",
}


def from_msb(text):
    """Issue notation (bits in transmission order) -> library layout (LSB first)."""
    b = bytes.fromhex(text)
    rev = lambda x, n: int(f"{x:0{n}b}"[::-1], 2)
    return [rev(b[0] & 0x0F, 4)] + [rev(x, 8) for x in b[1:]]


def main():
    esphome_raw = [v if i % 2 == 0 else -v for i, v in enumerate(RAW)]
    assert decode(esphome_raw) == RAW_STATE
    assert checksum(RAW_STATE) == RAW_STATE[10]
    assert setpoint(RAW_STATE) == 19

    encoded = encode(RAW_STATE)
    assert len(encoded) == len(esphome_raw) == 171
    assert all((a > 0) == (b > 0) for a, b in zip(encoded, esphome_raw))

    random.seed(1)
    noisy = [300, -8000] + [int(x * random.uniform(0.8, 1.2)) for x in esphome_raw]
    assert decode(noisy) == RAW_STATE

    for text in CAPTURES:
        s = frame(text)
        assert checksum(s) == s[10], text
        assert decode(encode(s)) == s, text

    for temp, text in SETPOINTS_MSB.items():
        s = from_msb(text)
        assert setpoint(s) == temp, temp
        assert checksum(s) == s[10], temp
        assert decode(encode(s)) == s, temp

    print(f"OK: raw capture, {len(CAPTURES)} remote frames, {len(SETPOINTS_MSB)} setpoints")


if __name__ == "__main__":
    main()

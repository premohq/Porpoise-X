#!/usr/bin/env python3
"""Porpoise - the boot's sound: the hum rising under the Sonar boot, and its pings.

    python3 tools/make-boot-sound.py

The same sound as the boot concepts page (Porpoise Boot Sonar.html), rendered
offline so the PS5 plays one clip in step with the animation
(src/ui_app_boot.cpp). The hum is tuned to the menu music it rises into:

    assets/sounds/boot-sonar.ogg         in C, for Soundscape
    assets/sounds/boot-sonar-legacy.ogg  in F, for Porpoise's original music

Needs numpy, scipy and ffmpeg (libvorbis).
"""
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

ROOT = Path(__file__).resolve().parent.parent
SR = 48000
LENGTH = 9.0
T_LAND = 4.3
PINGS = [0.3, 1.15, 1.85, 2.45, 2.95, 3.4]  # the boot's own (ui_app_boot.cpp kPings)


def t_axis():
    return np.arange(int(SR * LENGTH)) / SR


def ramp_exp(t, points):
    """Exponential ramps between (time, value) points, held after the last."""
    out = np.full_like(t, points[0][1])
    for (t0, v0), (t1, v1) in zip(points, points[1:]):
        m = (t >= t0) & (t < t1)
        k = (t[m] - t0) / (t1 - t0)
        out[m] = v0 * (v1 / v0) ** k
    out[t >= points[-1][0]] = points[-1][1]
    return out


def ramp_lin(t, points):
    return np.interp(t, [p[0] for p in points], [p[1] for p in points])


def target(t, start, value_from, tau):
    """Web Audio's setTargetAtTime toward zero, from start."""
    out = np.ones_like(t)
    m = t >= start
    out[m] = np.exp(-(t[m] - start) / tau)
    return np.where(m, value_from * out, np.nan)


def saw(freq, t, cents=0.0):
    f = freq * 2 ** (cents / 1200)
    ph = (f * t) % 1.0
    return 2 * ph - 1


def tri(freq, t):
    ph = (freq * t) % 1.0
    return 4 * np.abs(ph - 0.5) - 1


def biquad(kind, f0, q):
    w = 2 * np.pi * f0 / SR
    a = np.sin(w) / (2 * q)
    c = np.cos(w)
    if kind == "lp":
        b = [(1 - c) / 2, 1 - c, (1 - c) / 2]
    else:  # band-pass, constant peak gain
        b = [a, 0, -a]
    den = [1 + a, -2 * c, 1 - a]
    return np.array(b) / den[0], np.array(den) / den[0]


def sweep(x, kind, freqs, q, block=64):
    """x through a filter whose frequency follows freqs (per sample)."""
    y = np.zeros_like(x)
    zi = np.zeros(2)
    for i in range(0, len(x), block):
        b, a = biquad(kind, float(np.clip(freqs[i], 20, SR * 0.45)), q)
        y[i:i + block], zi = lfilter(b, a, x[i:i + block], zi=zi)
    return y


def hum(root, t):
    # (Oct 24, on the PS5: it starts already there, a TV's speakers being deaf
    # to the page's slow rise from nothing at 65 Hz)
    gain = ramp_exp(t, [(0, 0.05), (0.9, 0.16)])
    gain = np.where(t >= 0.9, ramp_lin(t, [(0.9, 0.16), (T_LAND, 0.3)]), gain)
    gain *= np.clip(t / 0.08, 0, 1)  # (no click at its start)
    tail = target(t, T_LAND + 0.35, 0.3, 0.9)
    gain = np.where(np.isnan(tail), gain, tail)
    trem = 1 + 0.12 * np.sin(2 * np.pi * 0.35 * t)
    lp = ramp_exp(t, [(0, 380), (T_LAND - 0.6, 1800), (T_LAND + 0.1, 3200), (T_LAND + 3, 380)])
    hi = ramp_lin(t, [(0, 0), (T_LAND, 0.06)])
    air = ramp_lin(t, [(1.5, 0), (T_LAND, 0.045)])
    body = 0.4 * np.sin(2 * np.pi * root * t) + 0.32 * np.sin(2 * np.pi * root * 2 * t)  # (the octave: heard on TVs)
    left = (body + 0.11 * saw(root * 2, t, -7) * 1.4 + 0.11 * saw(root * 2, t, 7) * 0.6 +
            0.07 * saw(root * 3, t, 4) * (t > 0.6) + hi * tri(root * 4, t) + air * np.sin(2 * np.pi * root * 4.5 * t))
    right = (body + 0.11 * saw(root * 2, t, -7) * 0.6 + 0.11 * saw(root * 2, t, 7) * 1.4 +
             0.07 * saw(root * 3, t, 4) * (t > 0.6) + hi * tri(root * 4, t) + air * np.sin(2 * np.pi * root * 4.5 * t + 0.4))
    out = []
    for ch in (left, right):
        out.append(sweep(ch, "lp", lp, 0.9) * trem * gain)
    # breath on top
    rng = np.random.default_rng(7)
    noise = rng.uniform(-1, 1, (2, len(t)))
    bp = ramp_exp(t, [(0, 500), (T_LAND, 5200)])
    ng = ramp_lin(t, [(0, 0), (T_LAND - 0.1, 0.05)])
    ntail = target(t, T_LAND + 0.1, 0.05, 0.5)
    ng = np.where(np.isnan(ntail), ng, ntail)
    for c in range(2):
        out[c] = out[c] + sweep(noise[c], "bp", bp, 0.7) * ng
    return np.array(out)


def pings(notes, t):
    out = np.zeros((2, len(t)))
    for i, (p, f) in enumerate(zip(PINGS, notes)):
        level = 0.24 if i == len(PINGS) - 1 else 0.16
        m = t >= p
        tt = t[m] - p
        env = np.where(tt < 0.01, 0.0001 * (level / 0.0001) ** np.minimum(tt / 0.01, 1),
                       level * (0.0001 / level) ** np.clip((tt - 0.01) / 1.59, 0, 1))
        env[tt > 1.6] = 0
        tone = np.sin(2 * np.pi * f * tt) + 0.25 * np.sin(2 * np.pi * f * 2.01 * tt) * (tt < 0.6)
        dry = np.zeros(len(t))
        dry[m] = tone * env
        # the echo: 0.28 s, fed back at 0.32 through a soft low-pass, each repeat
        # to the other side
        b, a = biquad("lp", 2400, 0.7)
        d = int(0.28 * SR)
        echo = dry.copy()
        side = i % 2
        out[0] += dry
        out[1] += dry
        for r in range(1, 7):
            echo = lfilter(b, a, np.concatenate([np.zeros(d), echo[:-d]])) * (1.0 if r == 1 else 0.32)
            out[side] += echo * 0.85
            out[1 - side] += echo * 0.35
            side = 1 - side
    return out


def compress(x, threshold_db=-14, ratio=4):
    level = np.max(np.abs(x), axis=0)
    # a smooth envelope (attack 3 ms, release 250 ms)
    env = np.zeros_like(level)
    a_att, a_rel = np.exp(-1 / (0.003 * SR)), np.exp(-1 / (0.25 * SR))
    e = 0.0
    for i in range(0, len(level), 32):
        v = level[i:i + 32].max()
        e = a_att * e + (1 - a_att) * v if v > e else a_rel * e + (1 - a_rel) * v
        env[i:i + 32] = e
    thr = 10 ** (threshold_db / 20)
    over = np.maximum(env / thr, 1.0)
    gain = over ** (1 / ratio - 1)
    return x * gain


def render(root, notes, path):
    t = t_axis()
    mix = hum(root, t) + pings(notes, t)
    mix = compress(mix) * 0.9
    # (the PS5 mixes this as a sound effect at the sounds' volume, the music at
    # its volume squared: this peak sits it a little over the music at 40%)
    mix *= 0.42 / np.max(np.abs(mix))
    fade = np.clip((LENGTH - t) / 0.5, 0, 1)
    mix *= fade
    pcm = (np.clip(mix, -1, 1) * 32767).astype(np.int16).T.copy()
    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "boot.wav"
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(SR)
            w.writeframes(pcm.tobytes())
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(wav), "-c:a", "libvorbis", "-q:a", "6", str(path)],
                       check=True)
    print(f"{path.relative_to(ROOT)}: {path.stat().st_size} bytes, peak {np.max(np.abs(mix)):.2f}")


def main() -> int:
    c = [523.25, 587.33, 659.25, 783.99, 880, 1046.5, 1174.66]
    f = [698.46, 783.99, 880, 1046.5, 1174.66, 1396.9, 1567.98]
    order = [0, 3, 2, 3, 5, 6]
    render(65.41, [c[i] for i in order], ROOT / "assets" / "sounds" / "boot-sonar.ogg")
    render(87.31, [f[i] for i in order], ROOT / "assets" / "sounds" / "boot-sonar-legacy.ogg")
    return 0


if __name__ == "__main__":
    sys.exit(main())

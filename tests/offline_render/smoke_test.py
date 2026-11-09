#!/usr/bin/env python3
"""T21 end-to-end smoke test (ctest): generate a short stereo wav, render it
through the offline_render tool, verify the output exists and is non-trivial.

Usage: smoke_test.py <path-to-offline_render>
"""

import os
import struct
import subprocess
import sys
import tempfile
import wave

SR = 48000
SECONDS = 2
BLOCK = 512


def make_input(path, sr, seconds):
    n = sr * seconds
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        frames = bytearray()
        for i in range(n):
            t = i / sr
            left = int(0.3 * 32767 * _sin(2 * 440 * t))
            right = int(0.3 * 32767 * _sin(2 * 554 * t))
            frames += struct.pack("<hh", left, right)
        w.writeframes(bytes(frames))


def _sin(x):
    # stdlib-only sine
    import math
    return math.sin(x)


def wav_info(path):
    with wave.open(path, "rb") as w:
        return (w.getnchannels(), w.getframerate(), w.getnframes())


def main():
    if len(sys.argv) < 2:
        print("usage: smoke_test.py <offline_render exe>", file=sys.stderr)
        return 2
    tool = sys.argv[1]
    if not os.path.exists(tool):
        print(f"tool not found: {tool}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        inp = os.path.join(tmp, "input.wav")
        out = os.path.join(tmp, "output.wav")
        out2 = os.path.join(tmp, "scan.wav")

        make_input(inp, SR, SECONDS)

        # 1) single block render
        r = subprocess.run(
            [tool, f"--in={inp}", f"--out={out}", f"--sr={SR}",
             f"--block={BLOCK}", f"--seconds={SECONDS}",
             "--param=b1_jitter_depth=5"],
            capture_output=True, text=True, timeout=600)
        if r.returncode != 0:
            print("render failed:\n" + r.stdout + r.stderr, file=sys.stderr)
            return 1
        if not os.path.exists(out) or os.path.getsize(out) < 1000:
            print("output missing or trivial", file=sys.stderr)
            return 1
        ch, sr, n = wav_info(out)
        if ch != 2 or sr != SR or n != SR * SECONDS:
            print(f"output format mismatch: ch={ch} sr={sr} n={n}", file=sys.stderr)
            return 1

        # 2) block-size scan (1 and 8192 edges)
        r = subprocess.run(
            [tool, f"--in={inp}", f"--out={out2}", f"--sr={SR}",
             "--seconds=1", "--block-scan=1,64,512,8192"],
            capture_output=True, text=True, timeout=600)
        if r.returncode != 0:
            print("block-scan failed:\n" + r.stdout + r.stderr, file=sys.stderr)
            return 1
        for b in (1, 64, 512, 8192):
            p = os.path.join(tmp, f"scan.b{b}.wav")
            if not os.path.exists(p) or os.path.getsize(p) < 1000:
                print(f"block-scan output missing: {p}", file=sys.stderr)
                return 1

        # 3) T33: sideband engine through the CLI (engine_version + sideband
        #    params must pass the unknown-id validation and render).
        sb = os.path.join(tmp, "sideband.wav")
        r = subprocess.run(
            [tool, f"--in={inp}", f"--out={sb}", f"--sr={SR}",
             "--seconds=1",
             "--param=engine_version=1",
             "--param=band_count=0",
             "--param=b1_sideband_enable=1",
             "--param=b1_sideband_depth=60",
             "--param=b1_sideband_fm_ratio=0.3",
             "--param=b1_sideband_roughness=50",
             "--param=b1_sideband_lowcut=300"],
            capture_output=True, text=True, timeout=600)
        if r.returncode != 0:
            print("sideband render failed:\n" + r.stdout + r.stderr, file=sys.stderr)
            return 1
        if not os.path.exists(sb) or os.path.getsize(sb) < 1000:
            print("sideband output missing or trivial", file=sys.stderr)
            return 1

        # 4) unknown sideband-ish id must still be rejected (validation intact)
        r = subprocess.run(
            [tool, f"--in={inp}", f"--out={sb}", f"--sr={SR}",
             "--seconds=1", "--param=b1_sideband_dept=60"],
            capture_output=True, text=True, timeout=600)
        if r.returncode == 0:
            print("unknown parameter id was accepted", file=sys.stderr)
            return 1

        # 5) audit P2-6: rates above the supported quality range (192k) are
        #    rejected at the CLI boundary (delay-line capacity / F0 window).
        r = subprocess.run(
            [tool, f"--in={inp}", f"--out={sb}", "--sr=384000", "--seconds=1"],
            capture_output=True, text=True, timeout=600)
        if r.returncode == 0:
            print("384 kHz was accepted despite the 192 kHz quality limit", file=sys.stderr)
            return 1

        print("offline_smoke OK")
        return 0


if __name__ == "__main__":
    sys.exit(main())

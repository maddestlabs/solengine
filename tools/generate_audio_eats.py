#!/usr/bin/env python3
"""
tools/generate_audio_eats.py
Packages physical card foley recordings into self-contained .eats audio soundbanks
with multi-sample variations (4 deal swipes, 2 place touchdowns, soft & loud shuffles)
using 22.05 kHz 16-bit mono downsampling, Deflate/zlib compression, and Base64 embedding.
"""

import io
import os
import zlib
import base64
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample

def process_samples(data, sr, target_sr=22050):
    peak = np.max(np.abs(data))
    if peak > 0:
        data = data / peak
    new_len = int(len(data) * target_sr / sr)
    s_resamp = resample(data, new_len)
    fade = int(target_sr * 0.002)
    s_resamp[:fade] *= np.linspace(0, 1, fade)
    s_resamp[-fade:] *= np.linspace(1, 0, fade)
    pcm16 = (np.clip(s_resamp, -1.0, 1.0) * 32767).astype(np.int16)
    buf = io.BytesIO()
    wavfile.write(buf, target_sr, pcm16)
    comp = zlib.compress(buf.getvalue(), 9)
    return base64.b64encode(comp).decode("ascii")

def main():
    # 1. Place variations: place.wav (0-160ms) and single.wav (90-240ms)
    sr, p1 = wavfile.read("cardsounds/place.wav")
    if p1.ndim == 2: p1 = p1.mean(axis=1)
    b64_place1 = process_samples(p1[:int(0.16 * sr)], sr)

    sr, p2 = wavfile.read("cardsounds/single.wav")
    if p2.ndim == 2: p2 = p2.mean(axis=1)
    b64_place2 = process_samples(p2[int(0.09 * sr):int(0.24 * sr)], sr)

    # 2. Deal variations: 4 distinct real card swipes from deal-series.wav
    sr, d = wavfile.read("cardsounds/deal-series.wav")
    if d.ndim == 2: d = d.mean(axis=1)
    deal_events = [(20020, 26973), (36828, 44749), (65692, 74053), (97812, 104765)]
    b64_deals = [process_samples(d[s0:s1], sr) for s0, s1 in deal_events]

    # 3. Shuffle variations: soft riffle and loud riffle
    sr, ss = wavfile.read("cardsounds/shuffle-soft.wav")
    if ss.ndim == 2: ss = ss.mean(axis=1)
    b64_ssoft = process_samples(ss[int(0.035 * sr):int(0.385 * sr)], sr)

    sr, sl = wavfile.read("cardsounds/shuffle-loud.wav")
    if sl.ndim == 2: sl = sl.mean(axis=1)
    b64_sloud = process_samples(sl[int(0.040 * sr):int(0.400 * sr)], sr)

    # 4. Snap / GUI / Tap variations: shuffle-part-2.wav and shuffle-part-3.wav
    def trim_transient(data, sample_rate, threshold_ratio=0.05, max_dur_sec=0.12):
        peak = np.max(np.abs(data))
        if peak <= 0:
            return data
        above = np.where(np.abs(data) >= peak * threshold_ratio)[0]
        start = max(0, int(above[0]) - int(0.002 * sample_rate)) if len(above) > 0 else 0
        end = min(len(data), start + int(max_dur_sec * sample_rate))
        return data[start:end]

    sr, sn1 = wavfile.read("cardsounds/shuffle-part-2.wav")
    if sn1.ndim == 2: sn1 = sn1.mean(axis=1)
    b64_snap1 = process_samples(trim_transient(sn1, sr), sr)

    sr, sn2 = wavfile.read("cardsounds/shuffle-part-3.wav")
    if sn2.ndim == 2: sn2 = sn2.mean(axis=1)
    b64_snap2 = process_samples(trim_transient(sn2, sr), sr)

    eats_content = f"""# ==============================================================================
# SolEngine Core Embedded Audio Foley (.eats)
# Default Authentic Table: Multi-Sample Physical Audio Soundbank
# Zero external .wav dependencies — 100% self-contained in Eatscript.
# ==============================================================================

audio.set_master_volume(0.85)
audio.set_master_variance(1.0) # Organic pitch, touch pressure, and variation cycling

# Physical DSP acoustic fallback parameters:
audio.set_physical_params({{
    velvet_density: 4200.0,
    edge_snap:      0.85,
    air_boost:      5.0,
    comb_delay:     0.00018,
    felt_damping:   7500.0,
    card_rigidity:  3200.0
}})

# --- Embedded Authentic Physical Card Drops (2 distinct touchdown recordings) ---
audio.embed_sound("place", \"\"\"{b64_place1}\"\"\")
audio.embed_sound("place", \"\"\"{b64_place2}\"\"\")

# --- Embedded Authentic Physical Deal Swipes (4 distinct card slide recordings) ---
audio.embed_sound("deal", \"\"\"{b64_deals[0]}\"\"\")
audio.embed_sound("deal", \"\"\"{b64_deals[1]}\"\"\")
audio.embed_sound("deal", \"\"\"{b64_deals[2]}\"\"\")
audio.embed_sound("deal", \"\"\"{b64_deals[3]}\"\"\")

# --- Embedded Authentic Physical Riffles (Soft and Loud cascades) ---
audio.embed_sound("shuffle_soft", \"\"\"{b64_ssoft}\"\"\")
audio.embed_sound("shuffle_loud", \"\"\"{b64_sloud}\"\"\")

# --- Embedded Authentic Physical Tap / GUI Snaps (2 distinct recordings) ---
audio.embed_sound("snap", \"\"\"{b64_snap1}\"\"\")
audio.embed_sound("snap", \"\"\"{b64_snap2}\"\"\")
"""

    out_paths = ["sounds/default.eats", "web/sounds/default.eats"]
    for p in out_paths:
        with open(p, "w", encoding="utf-8") as f:
            f.write(eats_content)
        print(f"Wrote {p} ({os.path.getsize(p)} bytes)")

if __name__ == "__main__":
    main()

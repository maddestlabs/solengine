#!/usr/bin/env python3
"""
tools/generate_audio_eats.py
Packages physical card foley recordings into self-contained .eats audio soundbanks
using 22.05 kHz 16-bit mono downsampling, Deflate/zlib compression, and Base64 embedding.
"""

import io
import os
import zlib
import base64
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample

def process_clip(path, t0, t1, target_sr=22050):
    sr, data = wavfile.read(path)
    if data.ndim == 2:
        data = data.mean(axis=1)
    data = data.astype(np.float32)
    if data.dtype == np.int16:
        data = data / 32768.0

    s = data[int(t0 * sr):int(t1 * sr)]
    peak = np.max(np.abs(s))
    if peak > 0:
        s = s / peak

    # Resample to target sample rate (22.05 kHz captures all card acoustic formants)
    new_len = int(len(s) * target_sr / sr)
    s_resamp = resample(s, new_len)

    # 2ms micro-fade to avoid clicks
    fade = int(target_sr * 0.002)
    s_resamp[:fade] *= np.linspace(0, 1, fade)
    s_resamp[-fade:] *= np.linspace(1, 0, fade)

    pcm16 = (np.clip(s_resamp, -1.0, 1.0) * 32767).astype(np.int16)
    buf = io.BytesIO()
    wavfile.write(buf, target_sr, pcm16)
    raw_wav = buf.getvalue()

    comp = zlib.compress(raw_wav, 9)
    b64 = base64.b64encode(comp).decode("ascii")
    return b64, len(raw_wav), len(comp)

def main():
    clips = {
        "place": ("cardsounds/place.wav", 0.0, 0.16),
        "deal": ("cardsounds/deal-series.wav", 0.88, 1.04),
        "shuffle_soft": ("cardsounds/shuffle-soft.wav", 0.035, 0.385),
        "shuffle_loud": ("cardsounds/shuffle-loud.wav", 0.040, 0.400)
    }

    b64_dict = {}
    for name, (path, t0, t1) in clips.items():
        if not os.path.exists(path):
            print(f"Error: {path} not found")
            return
        b64, raw_sz, comp_sz = process_clip(path, t0, t1)
        b64_dict[name] = b64
        print(f"Encoded '{name}': {raw_sz} B -> {comp_sz} B zlib -> {len(b64)} chars base64")

    # Generate sounds/default.eats
    eats_content = f"""# ==============================================================================
# SolEngine Core Embedded Audio Foley (.eats)
# Default Authentic Table: Physical Audio Resynthesis with Deflate+Base64
# Zero external .wav dependencies — 100% self-contained in Eatscript.
# ==============================================================================

audio.set_master_volume(0.85)
audio.set_master_variance(1.0)

# Physical DSP acoustic fallback parameters:
audio.set_physical_params({{
    velvet_density: 4200.0,
    edge_snap:      0.85,
    air_boost:      5.0,
    comb_delay:     0.00018,
    felt_damping:   7500.0,
    card_rigidity:  3200.0
}})

# Embedded authentic physical card recordings (22.05kHz 16-bit mono, zlib+base64):
audio.embed_sound("place", \"\"\"{b64_dict['place']}\"\"\")
audio.embed_sound("deal", \"\"\"{b64_dict['deal']}\"\"\")
audio.embed_sound("shuffle_soft", \"\"\"{b64_dict['shuffle_soft']}\"\"\")
audio.embed_sound("shuffle_loud", \"\"\"{b64_dict['shuffle_loud']}\"\"\")
"""

    out_paths = ["sounds/default.eats", "web/sounds/default.eats"]
    for p in out_paths:
        with open(p, "w", encoding="utf-8") as f:
            f.write(eats_content)
        print(f"Wrote {p} ({os.path.getsize(p)} bytes)")

if __name__ == "__main__":
    main()

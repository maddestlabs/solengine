#!/usr/bin/env python3
"""
tools/foley_resynthesize.py
===========================
Two-Stage Automated Procedural Foley Resynthesis & Statistical Variance Extractor.
Part of the SolEngine & Eatsbits DAW Procedural Audio Architecture.

Decomposes multi-event acoustic recordings (e.g. cardsounds/deal-series.wav) into:
  1. Thin-plate modal eigenmodes (frequencies, decay rates, Q-factors, gains)
  2. Pre-release friction peel duration & high-frequency spectral centroid
  3. Surface landing impact delay, low-frequency table thud, and double-tap probability
  4. Empirical statistical distribution (mean mu and deviation sigma across the sequence)

Outputs declarative Eatscript (.eats) presets ready for real-time synthesis in SolEngine
and modular procedural instruments in Eatsbits DAW.
"""

import argparse
import os
import sys
import numpy as np
import scipy.io.wavfile as wav
from scipy.signal import find_peaks, butter, sosfilt, hilbert

def load_wav(file_path):
    """Load WAV audio, return mono float32 array normalized to [-1.0, 1.0] and sample rate."""
    sr, data = wav.read(file_path)
    # If stereo/multichannel, average to mono
    if len(data.shape) > 1:
        mono = np.mean(data, axis=1)
    else:
        mono = data.astype(np.float32)

    # Convert integer types to float32 [-1, 1]
    if mono.dtype == np.int16:
        mono = mono / 32768.0
    elif mono.dtype == np.int32:
        mono = mono / 2147483648.0
    elif mono.dtype == np.uint8:
        mono = (mono - 128) / 128.0

    return mono.astype(np.float32), sr

def detect_events(mono, sr, min_event_gap_ms=100.0, energy_thresh_ratio=0.15):
    """
    Detect onsets in a multi-event sequence.
    Returns a list of dicts with onset_idx, peak_idx, and time in seconds.
    """
    # High-pass filter above 120 Hz to eliminate handling/DC/rumble
    sos_hp = butter(2, 120.0, btype='highpass', fs=sr, output='sos')
    filtered = sosfilt(sos_hp, mono)
    abs_sig = np.abs(filtered)

    # Moving RMS envelope over 5ms windows with 1ms hop
    win_len = max(4, int(sr * 0.005))
    hop_len = max(1, int(sr * 0.001))
    num_frames = (len(abs_sig) - win_len) // hop_len

    env = np.zeros(num_frames, dtype=np.float32)
    for i in range(num_frames):
        chunk = abs_sig[i * hop_len : i * hop_len + win_len]
        env[i] = np.sqrt(np.mean(chunk**2))

    max_env = np.max(env)
    threshold = max_env * energy_thresh_ratio

    # Find peaks in envelope separated by at least min_event_gap_ms
    min_dist_frames = int((min_event_gap_ms / 1000.0) * (sr / hop_len))
    peak_frames, properties = find_peaks(env, height=threshold, distance=min_dist_frames)

    events = []
    for pf in peak_frames:
        # Refine exact peak sample in raw audio around envelope frame
        search_start = max(0, pf * hop_len - win_len)
        search_end = min(len(mono), pf * hop_len + win_len * 2)
        local_peak_offset = np.argmax(np.abs(mono[search_start:search_end]))
        peak_sample = search_start + local_peak_offset
        peak_time = peak_sample / sr
        peak_amp = float(np.abs(mono[peak_sample]))

        # Find start of event (backtrack until envelope drops to 12% of peak)
        ev_peak_val = env[pf]
        start_frame = pf
        while start_frame > 0 and env[start_frame] > ev_peak_val * 0.12:
            start_frame -= 1
        start_sample = start_frame * hop_len

        # Find end of event (forward track until envelope drops below threshold)
        end_frame = pf
        while end_frame < len(env) - 1 and env[end_frame] > ev_peak_val * 0.08:
            end_frame += 1
        end_sample = min(len(mono), end_frame * hop_len + win_len)

        events.append({
            'start_sample': start_sample,
            'peak_sample': peak_sample,
            'end_sample': end_sample,
            'peak_time': peak_time,
            'peak_amp': peak_amp,
            'env_peak': float(ev_peak_val)
        })

    return events

def estimate_decay_rate(sig, sr, center_freq, bandwidth_hz=350.0):
    """
    Bandpass filter around center_freq, extract Hilbert envelope,
    and fit exponential decay rate alpha (sig ~ exp(-alpha * t)).
    Returns decay_sec (time constant tau = 1/alpha) and Q factor.
    """
    nyq = sr * 0.5
    f_low = max(20.0, center_freq - bandwidth_hz * 0.5)
    f_high = min(nyq - 20.0, center_freq + bandwidth_hz * 0.5)

    if f_high <= f_low:
        return 0.015, 8.0

    sos_bp = butter(2, [f_low, f_high], btype='bandpass', fs=sr, output='sos')
    filtered = sosfilt(sos_bp, sig)

    # Analytic signal via Hilbert transform
    analytic = hilbert(filtered)
    env = np.abs(analytic)

    # Find peak of envelope
    pk_idx = np.argmax(env)
    post_env = env[pk_idx:]
    if len(post_env) < 10:
        return 0.015, 8.0

    # Fit exponential decay over the region where envelope drops from 90% to 10%
    max_val = post_env[0]
    thresh_high = max_val * 0.90
    thresh_low = max_val * 0.10

    fit_indices = np.where((post_env <= thresh_high) & (post_env >= thresh_low))[0]
    if len(fit_indices) >= 5:
        t_axis = fit_indices / sr
        log_vals = np.log(np.maximum(1e-6, post_env[fit_indices]))
        # Linear regression: log(y) = -alpha * t + c
        slope, _ = np.polyfit(t_axis, log_vals, 1)
        alpha = -slope
        alpha = np.clip(alpha, 20.0, 500.0) # between 2ms and 50ms decay
        decay_sec = 1.0 / alpha
    else:
        # Fallback: estimate time to reach 1/e (36.8%)
        idx_e = np.where(post_env <= max_val * 0.368)[0]
        if len(idx_e) > 0:
            decay_sec = float(idx_e[0]) / sr
            decay_sec = np.clip(decay_sec, 0.003, 0.050)
            alpha = 1.0 / decay_sec
        else:
            decay_sec = 0.015
            alpha = 1.0 / decay_sec

    # Cardstock / paper fibers have strong internal damping; clamp Q to physical card limits [1.5, 12.0]
    # to avoid undamped sinusoidal ringing / glass harmonics.
    q_factor = float(np.clip(np.pi * center_freq * decay_sec, 1.5, 12.0))
    return float(decay_sec), q_factor

def analyze_event(mono, sr, event, num_modes=4):
    """
    Perform multi-modal decomposition of an individual event.
    Extracts:
      - Modes: list of {freq, decay, q, gain}
      - Peel friction: {duration, gain, centroid}
      - Landing: {delay, thud_freq, double_tap}
    """
    peak_s = event['peak_sample']

    # 1. Main transient & modal window: 0 to +30ms from peak
    t_start = peak_s
    t_end = min(len(mono), peak_s + int(sr * 0.035))
    modal_seg = mono[t_start:t_end]

    # Windowed FFT
    n_fft = 2048
    w = np.hanning(len(modal_seg))
    fft_vals = np.abs(np.fft.rfft(modal_seg * w, n=n_fft))
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)

    # Restrict to audible modal frequency band [1200 Hz .. 9000 Hz] for thin-plate card flex
    valid_band = (freqs >= 1200.0) & (freqs <= 9000.0)
    band_freqs = freqs[valid_band]
    band_mag = fft_vals[valid_band]

    # Find prominent modal peaks
    min_dist_bins = int(400.0 / (freqs[1] - freqs[0])) # at least 400Hz separation between modes
    p_indices, properties = find_peaks(band_mag, distance=max(1, min_dist_bins), prominence=np.max(band_mag) * 0.08)

    if len(p_indices) == 0:
        # Fallback to default peaks if signal is noisy
        top_freqs = [2600.0, 3900.0, 5100.0, 6800.0]
        top_gains = [1.0, 0.8, 0.6, 0.4]
    else:
        # Sort peaks by magnitude descending
        sorted_p = sorted(p_indices, key=lambda idx: -band_mag[idx])[:num_modes]
        # Sort selected modes in ascending order of frequency
        sorted_p = sorted(sorted_p, key=lambda idx: band_freqs[idx])

        max_mag = np.max(band_mag)
        top_freqs = [float(band_freqs[idx]) for idx in sorted_p]
        top_gains = [float(band_mag[idx] / max_mag) for idx in sorted_p]

    # While fewer than num_modes, pad with reasonable harmonics
    while len(top_freqs) < num_modes:
        next_f = top_freqs[-1] * 1.35
        top_freqs.append(min(9000.0, next_f))
        top_gains.append(top_gains[-1] * 0.6)

    # Estimate decay rate for each detected mode
    modes = []
    # Use wider window for decay estimation
    decay_seg = mono[peak_s : min(len(mono), peak_s + int(sr * 0.060))]
    for f, g in zip(top_freqs, top_gains):
        decay_sec, q = estimate_decay_rate(decay_seg, sr, f)
        modes.append({
            'freq': f,
            'decay': decay_sec,
            'q': q,
            'gain': g
        })

    # 2. Pre-snap thumb peel friction: adaptively trace backwards from peak_s - 1.5ms
    pre_window = int(sr * 0.035) # up to 35ms prior to peak
    peel_anchor = max(0, peak_s - int(sr * 0.0015))
    peel_search_start = max(0, peak_s - pre_window)
    pre_audio = mono[peel_search_start:peel_anchor]

    peel_dur = 0.016
    peel_amp = 0.0
    peel_centroid = 7200.0

    if len(pre_audio) > int(sr * 0.005):
        # Calculate moving RMS in 2ms chunks stepping backwards
        chunk_len = max(4, int(sr * 0.002))
        step_len = max(1, int(sr * 0.0005))
        back_steps = len(pre_audio) // step_len
        rms_profile = []
        for b in range(back_steps):
            c_end = len(pre_audio) - b * step_len
            c_start = max(0, c_end - chunk_len)
            if c_end <= c_start:
                break
            rms_profile.append(np.sqrt(np.mean(pre_audio[c_start:c_end]**2)))

        if len(rms_profile) > 0:
            max_peel_rms = np.max(rms_profile)
            peel_amp = float(np.max(np.abs(pre_audio)))
            # Find where RMS drops below 25% of max peel RMS or ambient floor (0.003)
            cutoff_step = len(rms_profile) - 1
            for idx, r_val in enumerate(rms_profile):
                if r_val < max(0.002, max_peel_rms * 0.25):
                    cutoff_step = idx
                    break
            peel_dur = max(0.006, cutoff_step * (step_len / sr))

            # Spectral centroid of the active peel segment
            active_peel_samples = int(peel_dur * sr)
            active_peel = pre_audio[-active_peel_samples:]
            if len(active_peel) > 16:
                peel_fft = np.abs(np.fft.rfft(active_peel * np.hanning(len(active_peel))))
                peel_freqs = np.fft.rfftfreq(len(active_peel), 1.0 / sr)
                peel_centroid = float(np.sum(peel_freqs * peel_fft) / max(1e-6, np.sum(peel_fft)))

    rel_peel_gain = peel_amp / max(1e-6, event['peak_amp'])

    # 3. Landing & surface impact detection: +15ms to +50ms relative to peak
    land_start = peak_s + int(sr * 0.012)
    land_end = min(len(mono), peak_s + int(sr * 0.055))
    land_seg = mono[land_start:land_end]

    landing_delay = 0.024
    landing_thud_freq = 320.0
    landing_gain = 0.45
    has_double_tap = False

    if len(land_seg) > 64:
        # Bandpass for surface thump (150Hz to 600Hz)
        sos_land = butter(2, [150.0, 600.0], btype='bandpass', fs=sr, output='sos')
        land_filtered = sosfilt(sos_land, land_seg)
        land_peaks, _ = find_peaks(np.abs(land_filtered), height=np.max(np.abs(land_filtered))*0.4, distance=int(sr * 0.006))

        if len(land_peaks) > 0:
            first_peak_s = land_peaks[0]
            landing_delay = (land_start + first_peak_s - peak_s) / sr
            landing_gain = float(np.max(np.abs(land_filtered)) / max(1e-6, event['peak_amp']))
            if len(land_peaks) > 1:
                has_double_tap = True

    return {
        'modes': modes,
        'peel': {
            'duration': peel_dur,
            'gain': float(np.clip(rel_peel_gain, 0.10, 0.60)),
            'centroid': float(np.clip(peel_centroid, 4500.0, 9500.0))
        },
        'landing': {
            'delay': float(np.clip(landing_delay, 0.014, 0.045)),
            'gain': float(np.clip(landing_gain, 0.20, 0.85)),
            'thud_freq': landing_thud_freq,
            'double_tap': has_double_tap
        }
    }

def aggregate_statistics(event_results, num_modes=4):
    """
    Compute mean (mu) and standard deviation (sigma) for all extracted parameters.
    """
    N = len(event_results)
    if N == 0:
        raise ValueError("No events to aggregate.")

    # 1. Modes aggregation
    mode_stats = []
    for m in range(num_modes):
        freqs = [res['modes'][m]['freq'] for res in event_results]
        decays = [res['modes'][m]['decay'] for res in event_results]
        gains = [res['modes'][m]['gain'] for res in event_results]
        qs = [res['modes'][m]['q'] for res in event_results]

        f_mean, f_std = float(np.mean(freqs)), float(np.std(freqs))
        d_mean, d_std = float(np.mean(decays)), float(np.std(decays))
        g_mean = float(np.mean(gains))
        q_mean = float(np.mean(qs))

        mode_stats.append({
            'freq_base': f_mean,
            'freq_dev': f_std / max(1.0, f_mean),
            'decay_base': d_mean,
            'decay_dev': d_std / max(1e-4, d_mean),
            'gain': g_mean,
            'q': q_mean
        })

    # 2. Peel friction aggregation
    peel_durs = [res['peel']['duration'] for res in event_results]
    peel_gains = [res['peel']['gain'] for res in event_results]
    peel_centroids = [res['peel']['centroid'] for res in event_results]

    peel_stats = {
        'duration_base': float(np.mean(peel_durs)),
        'duration_dev': float(np.std(peel_durs) / max(1e-4, np.mean(peel_durs))),
        'gain_base': float(np.mean(peel_gains)),
        'gain_dev': float(np.std(peel_gains) / max(1e-4, np.mean(peel_gains))),
        'centroid': float(np.mean(peel_centroids))
    }

    # 3. Landing aggregation
    land_delays = [res['landing']['delay'] for res in event_results]
    land_gains = [res['landing']['gain'] for res in event_results]
    double_taps = [1.0 if res['landing']['double_tap'] else 0.0 for res in event_results]

    landing_stats = {
        'delay_base': float(np.mean(land_delays)),
        'delay_dev': float(np.std(land_delays) / max(1e-4, np.mean(land_delays))),
        'gain_base': float(np.mean(land_gains)),
        'thud_freq': 320.0,
        'double_tap_chance': float(np.mean(double_taps))
    }

    return {
        'num_events': N,
        'modes': mode_stats,
        'peel': peel_stats,
        'landing': landing_stats
    }

def generate_eats_script(stats, sound_name="card_deal_series", source_wav=""):
    """
    Format extracted parameters into declarative Eatscript (.eats) code.
    """
    lines = []
    lines.append("# ==============================================================================")
    lines.append(f"# SolEngine & Eatsbits Procedural Foley Preset: {sound_name}")
    if source_wav:
        lines.append(f"# Auto-synthesized from {os.path.basename(source_wav)}")
    lines.append(f"# Empirical population: {stats['num_events']} human events analyzed")
    lines.append("# Thin-plate physical modal resonators + multi-stage friction & landing")
    lines.append("# ==============================================================================")
    lines.append("")
    lines.append("audio.set_master_volume(0.85)")
    lines.append("audio.set_master_variance(1.0) # 1.0 = authentic human deviation, 0.0 = deterministic")
    lines.append("")
    lines.append(f'audio.define_sound("{sound_name}", {{')
    lines.append("    # 4-pole thin-plate cardboard flexural eigenmodes:")
    lines.append("    modes: [")
    for idx, m in enumerate(stats['modes']):
        comma = "," if idx < len(stats['modes']) - 1 else ""
        lines.append(f"        {{ freq_base: {m['freq_base']:6.1f}, freq_dev: {m['freq_dev']:.3f}, decay_base: {m['decay_base']:.4f}, decay_dev: {m['decay_dev']:.3f}, gain: {m['gain']:.2f}, q: {m['q']:4.1f} }}{comma}")
    lines.append("    ],")
    lines.append("")
    lines.append("    # Pre-snap thumb peel friction (high-frequency epidermal shear):")
    p = stats['peel']
    lines.append("    peel_friction: {")
    lines.append(f"        duration_base: {p['duration_base']:.4f},")
    lines.append(f"        duration_dev:  {p['duration_dev']:.3f},")
    lines.append(f"        filter_cutoff: {p['centroid']:.1f},")
    lines.append(f"        gain_base:     {p['gain_base']:.3f}")
    lines.append("    },")
    lines.append("")
    lines.append("    # Baize / table felt landing & secondary micro-impacts:")
    l = stats['landing']
    lines.append("    landing: {")
    lines.append(f"        delay_base:        {l['delay_base']:.4f},")
    lines.append(f"        delay_dev:         {l['delay_dev']:.3f},")
    lines.append(f"        thud_frequency:    {l['thud_freq']:.1f},")
    lines.append(f"        gain_base:         {l['gain_base']:.3f},")
    lines.append(f"        double_tap_chance: {l['double_tap_chance']:.2f}")
    lines.append("    }")
    lines.append("})")
    lines.append("")
    return "\n".join(lines)

def main():
    parser = argparse.ArgumentParser(description="Automated Foley Resynthesis & Statistical Variance Extractor")
    parser.add_argument("input_wavs", nargs="+", help="One or more input reference WAV files")
    parser.add_argument("-o", "--output", default="web/sounds/authentic_deal_series.eats", help="Output .eats file path")
    parser.add_argument("--name", default="", help="Sound name in Eatscript definition (defaults to filename)")
    parser.add_argument("--min-gap", type=float, default=60.0, help="Minimum gap between events in ms (default 60ms)")
    parser.add_argument("-n", "--num-modes", type=int, default=4, help="Number of modal resonance poles to extract (default 4)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Print detailed diagnostic tables")
    args = parser.parse_args()

    all_event_results = []
    source_names = []

    for input_file in args.input_wavs:
        input_path = os.path.abspath(input_file)
        if not os.path.exists(input_path):
            print(f"Warning: Input file not found: {input_path}", file=sys.stderr)
            continue

        source_names.append(os.path.basename(input_path))
        print(f"[*] Loading reference audio: {input_path}")
        mono, sr = load_wav(input_path)
        total_dur = len(mono) / sr
        print(f"[*] Loaded {len(mono)} samples ({total_dur:.3f}s) @ {sr} Hz")

        print(f"[*] Performing transient onset detection on {os.path.basename(input_path)}...")
        events = detect_events(mono, sr, min_event_gap_ms=args.min_gap)
        print(f"[+] Detected {len(events)} discrete events:")

        for idx, ev in enumerate(events):
            res = analyze_event(mono, sr, ev, num_modes=args.num_modes)
            all_event_results.append(res)
            m_str = ", ".join([f"{m['freq']:.0f}Hz" for m in res['modes']])
            print(f"    Event {len(all_event_results):2d} (t={ev['peak_time']:5.3f}s): Peak={ev['peak_amp']:.3f} | Modes: [{m_str}] | Peel: {res['peel']['duration']*1000:4.1f}ms | Landing: +{res['landing']['delay']*1000:4.1f}ms")

    if not all_event_results:
        print("Error: No events detected across all input files.", file=sys.stderr)
        sys.exit(1)

    print(f"\n[*] Computing empirical population statistics across {len(all_event_results)} total events from {len(source_names)} files...")
    stats = aggregate_statistics(all_event_results, num_modes=args.num_modes)

    print("\n" + "="*70)
    print(f"  COMBINED MODAL & VARIANCE PROFILE ({stats['num_events']} events from {', '.join(source_names)})")
    print("="*70)
    print("  Resonant Mode   Base Freq (Hz)   Freq Dev (sigma)   Decay (ms)   Q Factor   Rel Gain")
    print("  -------------   --------------   ----------------   ----------   --------   --------")
    for i, m in enumerate(stats['modes']):
        print(f"     Mode {i+1}       {m['freq_base']:7.1f} Hz       +/- {m['freq_dev']*100:4.1f}%          {m['decay_base']*1000:5.1f} ms    {m['q']:5.1f}     {m['gain']:.2f}")

    print("\n  Multi-Stage Physical Mechanics:")
    print(f"    - Pre-Snap Peel Friction : {stats['peel']['duration_base']*1000:4.1f} ms (+/- {stats['peel']['duration_dev']*100:4.1f}%), Centroid: {stats['peel']['centroid']:4.0f} Hz")
    print(f"    - Baize Landing Delay    : +{stats['landing']['delay_base']*1000:4.1f} ms (+/- {stats['landing']['delay_dev']*100:4.1f}%), Thud Freq: {stats['landing']['thud_freq']:.0f} Hz")
    print(f"    - Double-Tap Chance      : {stats['landing']['double_tap_chance']*100:4.1f}%")
    print("="*70)

    sname = args.name if args.name else os.path.splitext(os.path.basename(args.output))[0]
    eats_content = generate_eats_script(stats, sound_name=sname, source_wav=" + ".join(source_names))

    out_path = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(eats_content)

    print(f"\n[+] Successfully exported declarative preset to:\n    --> {out_path}\n")

if __name__ == "__main__":
    main()

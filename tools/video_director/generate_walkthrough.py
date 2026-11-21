#!/usr/bin/env python3
"""
SolEngine Video Director Pipeline
Generates cached ElevenLabs narration WAVs, YouTube .srt captions,
and a SolEngine --director timeline (.dir) from a storyboard JSON.
"""

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import urllib.error
import urllib.request
import wave

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CACHE_DIR = os.path.join(SCRIPT_DIR, ".cache")
OUT_ROOT = os.path.join(SCRIPT_DIR, "out")
STORYBOARD_DIR = os.path.join(SCRIPT_DIR, "storyboards")

DEFAULT_MODEL = "eleven_multilingual_v2"
SAMPLE_RATE = 44100


def wrap_pcm_to_wav(pcm_bytes: bytes, wav_path: str, sample_rate: int = SAMPLE_RATE) -> None:
    os.makedirs(os.path.dirname(wav_path), exist_ok=True)
    with wave.open(wav_path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)  # 16-bit PCM
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_bytes)


def make_silent_wav(wav_path: str, duration_sec: float, sample_rate: int = SAMPLE_RATE) -> None:
    frames = max(1, int(round(duration_sec * sample_rate)))
    wrap_pcm_to_wav(b"\x00\x00" * frames, wav_path, sample_rate)


def get_audio_duration(path: str) -> float:
    if path.lower().endswith(".wav"):
        with wave.open(path, "rb") as wf:
            frames = wf.getnframes()
            rate = wf.getframerate()
            return frames / float(rate) if rate > 0 else 0.0
    # Parse MPEG-1 Layer III frame headers for exact MP3 duration
    with open(path, "rb") as f:
        data = f.read()
    pos = 0
    if data[:3] == b"ID3" and len(data) >= 10:
        tag_size = ((data[6] & 0x7F) << 21) | ((data[7] & 0x7F) << 14) | ((data[8] & 0x7F) << 7) | (data[9] & 0x7F)
        pos = 10 + tag_size
    bitrates_v1_l3 = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0]
    sr_v1 = [44100, 48000, 32000, 0]
    total_sec = 0.0
    while pos + 4 <= len(data):
        if data[pos] == 0xFF and (data[pos + 1] & 0xE0) == 0xE0:
            ver = (data[pos + 1] >> 3) & 0x03
            layer = (data[pos + 1] >> 1) & 0x03
            br_idx = (data[pos + 2] >> 4) & 0x0F
            sr_idx = (data[pos + 2] >> 2) & 0x03
            pad = (data[pos + 2] >> 1) & 0x01
            if ver == 3 and layer == 1 and 0 < br_idx < 15 and sr_idx < 3:
                br = bitrates_v1_l3[br_idx] * 1000
                sr = sr_v1[sr_idx]
                frame_len = (144 * br) // sr + pad
                if frame_len > 0:
                    total_sec += 1152.0 / float(sr)
                    pos += frame_len
                    continue
        pos += 1
    return total_sec if total_sec > 0.0 else (len(data) / 16000.0)


def synthesize_elevenlabs(text: str, voice_id: str, model_id: str, api_key: str, out_mp3: str) -> None:
    url = f"https://api.elevenlabs.io/v1/text-to-speech/{voice_id}?output_format=mp3_{SAMPLE_RATE}_128"
    payload = json.dumps({
        "text": text,
        "model_id": model_id,
        "voice_settings": {
            "stability": 0.45,
            "similarity_boost": 0.78,
            "style": 0.35,
            "use_speaker_boost": True
        }
    }).encode("utf-8")

    req = urllib.request.Request(
        url,
        data=payload,
        method="POST",
        headers={
            "xi-api-key": api_key,
            "Content-Type": "application/json",
            "Accept": "audio/mpeg",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            mp3_bytes = resp.read()
        os.makedirs(os.path.dirname(out_mp3), exist_ok=True)
        with open(out_mp3, "wb") as f:
            f.write(mp3_bytes)
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"ElevenLabs API error {e.code}: {body}") from e


def format_srt_time(seconds: float) -> str:
    ms_total = max(0, int(round(seconds * 1000.0)))
    hrs = ms_total // 3600000
    mins = (ms_total % 3600000) // 60000
    secs = (ms_total % 60000) // 1000
    ms = ms_total % 1000
    return f"{hrs:02d}:{mins:02d}:{secs:02d},{ms:03d}"


def stitch_master_wav(segments: list[tuple[str, float]], out_wav: str) -> None:
    os.makedirs(os.path.dirname(out_wav), exist_ok=True)
    with wave.open(out_wav, "wb") as out_wf:
        out_wf.setnchannels(1)
        out_wf.setsampwidth(2)
        out_wf.setframerate(SAMPLE_RATE)
        for wav_path, pad_sec in segments:
            with wave.open(wav_path, "rb") as in_wf:
                if in_wf.getframerate() == SAMPLE_RATE and in_wf.getnchannels() == 1 and in_wf.getsampwidth() == 2:
                    out_wf.writeframes(in_wf.readframes(in_wf.getnframes()))
                else:
                    raise RuntimeError(f"Unexpected WAV format in {wav_path}")
            pad_frames = max(0, int(round(pad_sec * SAMPLE_RATE)))
            if pad_frames > 0:
                out_wf.writeframes(b"\x00\x00" * pad_frames)


def scaffold_from_eats(eats_path: str) -> str:
    if not os.path.exists(eats_path):
        raise FileNotFoundError(f"Cannot find .eats file: {eats_path}")
    with open(eats_path, "r", encoding="utf-8") as f:
        src = f.read()

    slug = os.path.splitext(os.path.basename(eats_path))[0]
    m_name = re.search(r'^name\s*=\s*"([^"]+)"', src, re.MULTILINE)
    game_name = m_name.group(1) if m_name else slug.replace("_", " ").title()

    m_seed = re.search(r'^demo_seed\s*=\s*(\d+)', src, re.MULTILINE)
    demo_seed = int(m_seed.group(1)) if m_seed else 1

    m_goal = re.search(r'doc_goal\(\s*"([^"]+)"', src)
    goal_text = m_goal.group(1) if m_goal else f"Win {game_name}."

    zones = re.findall(r'doc_zone\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"', src)

    beats = [
        {
            "action": "reset_camera",
            "text": f"Welcome back to the table! Today we're exploring {game_name}.",
            "pad_sec": 0.5
        },
        {
            "action": "highlight_zone",
            "target_zone": int(zones[2][0]) if len(zones) > 2 else (int(zones[0][0]) if zones else 0),
            "text": f"Your primary objective is simple: {goal_text}",
            "pad_sec": 0.45
        },
    ]
    for z_idx_str, z_title, z_desc in zones[:3]:
        beats.append({
            "action": "highlight_zone",
            "target_zone": int(z_idx_str),
            "text": f"Here we have the {z_title}: {z_desc}",
            "pad_sec": 0.45
        })
    beats.append({
        "action": "play_moves",
        "move_count": 4,
        "text": "Let's watch a few opening moves in motion using SolEngine's built-in solver.",
        "pad_sec": 0.6
    })
    beats.append({
        "action": "reset_camera",
        "text": f"Deal yourself a fresh seed of {game_name}, and best of luck!",
        "pad_sec": 0.8
    })

    os.makedirs(STORYBOARD_DIR, exist_ok=True)
    out_json = os.path.join(STORYBOARD_DIR, f"{slug}.json")
    data = {
        "game_slug": slug,
        "game_name": game_name,
        "seed": demo_seed,
        "voice_id": "YOUR_ELEVENLABS_VOICE_ID",
        "model_id": DEFAULT_MODEL,
        "beats": beats,
    }
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    return out_json


def load_dotenv_if_present() -> None:
    env_path = os.path.join(SCRIPT_DIR, ".env")
    if not os.path.exists(env_path):
        return
    with open(env_path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            k = k.strip()
            v = v.strip().strip('"').strip("'")
            if k and k not in os.environ:
                os.environ[k] = v


def build_walkthrough(storyboard_path: str, voice_override: str | None, dry_run: bool, audio_dir: str | None = None) -> None:
    load_dotenv_if_present()
    with open(storyboard_path, "r", encoding="utf-8") as f:
        sb = json.load(f)

    slug = sb.get("game_slug") or os.path.splitext(os.path.basename(storyboard_path))[0]
    voice_id = voice_override or sb.get("voice_id", "")
    model_id = sb.get("model_id", DEFAULT_MODEL)
    seed = sb.get("seed", 1)
    beats = sb.get("beats", [])

    # Check if any beat actually needs the ElevenLabs API
    def resolve_local_wav(idx: int, beat: dict) -> str:
        explicit = beat.get("wav", "").strip()
        if explicit:
            if not os.path.isabs(explicit):
                explicit = os.path.join(os.path.dirname(os.path.abspath(storyboard_path)), explicit)
            if os.path.exists(explicit):
                return explicit
        if audio_dir:
            for cand in (f"{idx:02d}.wav", f"{idx}.wav", f"beat_{idx:02d}.wav"):
                p = os.path.join(audio_dir, cand)
                if os.path.exists(p):
                    return p
        return ""

    needs_api = not dry_run and any(
        beat.get("text", "").strip() and not resolve_local_wav(i, beat)
        for i, beat in enumerate(beats)
    )

    api_key = os.environ.get("ELEVENLABS_API_KEY", "").strip()
    if needs_api:
        if not api_key:
            raise RuntimeError("ELEVENLABS_API_KEY environment variable is not set (or pass --dry-run to test).")
        if not voice_id or voice_id == "YOUR_ELEVENLABS_VOICE_ID":
            raise RuntimeError("Set voice_id in the storyboard JSON or pass --voice <VOICE_ID>.")

    os.makedirs(CACHE_DIR, exist_ok=True)
    out_dir = os.path.join(OUT_ROOT, slug)
    os.makedirs(out_dir, exist_ok=True)

    timeline_lines = [
        f"# SolEngine Director Timeline for {sb.get('game_name', slug)}",
        "# Format: BEAT|action|durationSec|targetZone|moveCount|wavPath|caption|fromPile|cardIndex|toPile",
    ]
    srt_entries = []
    master_segments = []
    cursor_sec = 0.0

    for idx, beat in enumerate(beats):
        action = beat.get("action", "idle")
        text = beat.get("text", "").strip()
        target_zone = int(beat.get("target_zone", -1))
        move_count = int(beat.get("move_count", 1))
        from_pile = int(beat.get("from", -1))
        card_idx = int(beat.get("card_index", -1))
        to_pile = int(beat.get("to", -1))
        ghost_cards = beat.get("ghost_cards", "").strip()
        ghost_delay = float(beat.get("ghost_delay", 0.25))
        ghost_pile = int(beat.get("ghost_pile", -1))
        pad_sec = float(beat.get("pad_sec", 0.45))

        local_wav = resolve_local_wav(idx, beat)
        if local_wav:
            wav_path = local_wav
            speech_sec = get_audio_duration(wav_path)
        elif text:
            key = hashlib.sha256(f"{voice_id}|{model_id}|{text}".encode("utf-8")).hexdigest()[:20]
            mode_tag = "dry" if dry_run else "el"
            ext = ".wav" if dry_run else ".mp3"
            wav_path = os.path.join(CACHE_DIR, f"{slug}_{idx:02d}_{mode_tag}_{key}{ext}")
            if not os.path.exists(wav_path):
                if dry_run:
                    est_words = max(1, len(text.split()))
                    est_sec = max(1.2, est_words / 2.4)
                    make_silent_wav(wav_path, est_sec)
                else:
                    print(f"[ElevenLabs] Synthesizing beat {idx + 1}/{len(beats)}...")
                    synthesize_elevenlabs(text, voice_id, model_id, api_key, wav_path)
            speech_sec = get_audio_duration(wav_path)
        else:
            wav_path = ""
            speech_sec = float(beat.get("duration_sec", 2.0))

        total_sec = speech_sec + pad_sec
        clean_caption = text.replace("|", "-").replace("\n", " ")
        abs_wav = os.path.abspath(wav_path).replace("\\", "/") if wav_path else ""
        timeline_lines.append(
            f"BEAT|{action}|{total_sec:.3f}|{target_zone}|{move_count}|{abs_wav}|{clean_caption}|{from_pile}|{card_idx}|{to_pile}|{ghost_cards}|{ghost_delay:.2f}|{ghost_pile}"
        )

        if text:
            srt_entries.append((len(srt_entries) + 1, cursor_sec, cursor_sec + speech_sec, text))
            if wav_path.lower().endswith(".wav"):
                master_segments.append((wav_path, pad_sec))
        cursor_sec += total_sec

    timeline_path = os.path.join(out_dir, "timeline.dir")
    with open(timeline_path, "w", encoding="utf-8") as f:
        f.write("\n".join(timeline_lines) + "\n")

    srt_path = os.path.join(out_dir, "captions.srt")
    with open(srt_path, "w", encoding="utf-8") as f:
        for num, t0, t1, txt in srt_entries:
            f.write(f"{num}\n{format_srt_time(t0)} --> {format_srt_time(t1)}\n{txt}\n\n")

    if master_segments:
        stitch_master_wav(master_segments, os.path.join(out_dir, "narration_full.wav"))

    print(f"\n[Done] Total duration: {cursor_sec:.2f}s")
    print(f"  Timeline : {timeline_path}")
    print(f"  Subtitles: {srt_path}")
    print("\nRun SolEngine to record:")
    print(f'  build\\Release\\solengine.exe --game {slug} --seed {seed} --director "{timeline_path}" --hide-hud --width 1920 --height 1080')


def main() -> int:
    parser = argparse.ArgumentParser(description="SolEngine Walkthrough Video Director")
    parser.add_argument("storyboard", nargs="?", help="Path to storyboard JSON")
    parser.add_argument("--scaffold", help="Generate a starter storyboard JSON from a games/*.eats file")
    parser.add_argument("--voice", help="Override ElevenLabs voice_id")
    parser.add_argument("--audio-dir", help="Optional folder of pre-made/edited WAVs (00.wav, 01.wav, ...) to skip API calls")
    parser.add_argument("--dry-run", action="store_true", help="Use silent placeholder WAVs (no API call)")
    args = parser.parse_args()

    if args.scaffold:
        out_json = scaffold_from_eats(args.scaffold)
        print(f"Scaffolded storyboard: {out_json}")
        return 0

    if not args.storyboard:
        parser.print_help()
        return 1

    build_walkthrough(args.storyboard, args.voice, args.dry_run, args.audio_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())

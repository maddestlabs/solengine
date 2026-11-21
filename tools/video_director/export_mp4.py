#!/usr/bin/env python3
"""
SolEngine 4K/60fps Non-Realtime Video Exporter
Drives solengine.exe --export-video (deterministic fixed-timestep WebGPU offscreen
capture + sample-accurate offline AudioFoleyEngine + ElevenLabs mix) and encodes
the final MP4 via ffmpeg with zero screen-capture generation loss.
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
OUT_ROOT = os.path.join(SCRIPT_DIR, "out")


def find_ffmpeg() -> str | None:
    found = shutil.which("ffmpeg")
    if found:
        return found
    local_app = os.environ.get("LOCALAPPDATA", "")
    if local_app:
        winget_link = os.path.join(local_app, "Microsoft", "WinGet", "Links", "ffmpeg.exe")
        if os.path.exists(winget_link):
            return winget_link
        for p in glob.glob(os.path.join(local_app, "Microsoft", "WinGet", "Packages", "Gyan.FFmpeg*", "*", "bin", "ffmpeg.exe")):
            if os.path.exists(p):
                return p
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Export SolEngine walkthrough to MP4 via ffmpeg")
    parser.add_argument("slug", help="Game slug (e.g. klondike_draw_1)")
    parser.add_argument("--seed", type=int, default=2, help="Deal seed (default: 2)")
    parser.add_argument("--width", type=int, default=3840, help="Output width (default: 3840 for 4K)")
    parser.add_argument("--height", type=int, default=2160, help="Output height (default: 2160 for 4K)")
    parser.add_argument("--fps", type=int, default=60, help="Frame rate (default: 60)")
    parser.add_argument("--crf", type=int, default=14, help="x264 CRF quality (lower = higher quality, default: 14)")
    parser.add_argument("--keep-raw", action="store_true", help="Keep frames.raw and audio.wav after encoding")
    args = parser.parse_args()

    ffmpeg_bin = find_ffmpeg()
    if not ffmpeg_bin:
        print("Error: ffmpeg not found on PATH.", file=sys.stderr)
        print("Install it once via:  winget install Gyan.FFmpeg", file=sys.stderr)
        return 1

    exe_path = os.path.join(REPO_ROOT, "build", "Release", "solengine.exe")
    if not os.path.exists(exe_path):
        print(f"Error: {exe_path} not found. Build Release first.", file=sys.stderr)
        return 1

    game_out = os.path.join(OUT_ROOT, args.slug)
    timeline_path = os.path.join(game_out, "timeline.dir")
    if not os.path.exists(timeline_path):
        print(f"Error: {timeline_path} not found. Run generate_walkthrough.py first.", file=sys.stderr)
        return 1

    raw_dir = os.path.join(game_out, "_raw")
    os.makedirs(raw_dir, exist_ok=True)

    print(f"[1/2] Streaming {args.width}x{args.height} @ {args.fps}fps directly into ffmpeg...")
    log_path = os.path.join(raw_dir, "export.log")
    if os.path.exists(log_path):
        os.remove(log_path)
    cmd_render = [
        exe_path,
        "--game", args.slug,
        "--seed", str(args.seed),
        "--director", timeline_path,
        "--export-video", raw_dir,
        "--ffmpeg-bin", ffmpeg_bin,
        "--width", str(args.width),
        "--height", str(args.height),
        "--fps", str(args.fps),
    ]
    proc = subprocess.Popen(cmd_render, cwd=REPO_ROOT)
    last_pos = 0
    while proc.poll() is None:
        if os.path.exists(log_path):
            with open(log_path, "r", encoding="utf-8", errors="replace") as lf:
                lf.seek(last_pos)
                chunk = lf.read()
                if chunk:
                    print(chunk, end="", flush=True)
                    last_pos = lf.tell()
        import time
        time.sleep(0.5)
    if os.path.exists(log_path):
        with open(log_path, "r", encoding="utf-8", errors="replace") as lf:
            lf.seek(last_pos)
            chunk = lf.read()
            if chunk:
                print(chunk, end="", flush=True)
    if proc.returncode != 0:
        print("Error: SolEngine offline export failed.", file=sys.stderr)
        return proc.returncode

    video_only = os.path.join(raw_dir, "video_only.mp4")
    audio_wav = os.path.join(raw_dir, "audio.wav")
    out_mp4 = os.path.join(game_out, f"{args.slug}_{args.width}x{args.height}_{args.fps}fps.mp4")

    print(f"[2/2] Muxing audio + video -> {out_mp4} ...")
    cmd_ffmpeg = [
        ffmpeg_bin, "-y",
        "-i", video_only,
        "-i", audio_wav,
        "-c:v", "copy",
        "-c:a", "aac",
        "-b:a", "320k",
        "-movflags", "+faststart",
        out_mp4,
    ]
    enc = subprocess.run(cmd_ffmpeg, cwd=REPO_ROOT)
    if enc.returncode != 0:
        print("Error: ffmpeg muxing failed.", file=sys.stderr)
        return enc.returncode

    if not args.keep_raw:
        for p in (video_only, audio_wav):
            if os.path.exists(p):
                os.remove(p)

    thumb_jpg = os.path.join(game_out, "thumbnail_1280x720.jpg")
    subprocess.run([
        ffmpeg_bin, "-y", "-loglevel", "error",
        "-ss", "2.0", "-i", out_mp4,
        "-frames:v", "1", "-update", "1",
        "-vf", "scale=1280:720:flags=lanczos", "-q:v", "2",
        thumb_jpg,
    ], cwd=REPO_ROOT)

    print(f"\n[Done] Exported: {out_mp4}")
    if os.path.exists(thumb_jpg):
        print(f"       Thumbnail: {thumb_jpg}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

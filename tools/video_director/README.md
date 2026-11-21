# SolEngine Video Director (`tools/video_director`)

Automated ElevenLabs narration, YouTube `.srt` captions, deterministic `--director` playback, and non-realtime 4K/60fps WebGPU-to-`ffmpeg` MP4 export.

---

## 1. One-Time Setup
1. **ElevenLabs API Key:** Add `ELEVENLABS_API_KEY=sk_...` to `tools/video_director/.env` (gitignored).
   * Required key permissions: **Text to Speech → Access** (and optionally **Voices → Read**).
2. **FFmpeg (for MP4 export):**
   ```powershell
   winget install Gyan.FFmpeg
   ```
   (`export_mp4.py` auto-detects `ffmpeg.exe` on `PATH` or in WinGet's `Links`/`Packages` folders.)

---

## 2. Creating a Walkthrough Storyboard

### Option A: Inspect `demo_seeds` with `--dump-moves`
Find a seed with a clean, organic human opening:
```powershell
build\Release\solengine.exe --game klondike_draw_1 --seed 26 --dump-moves
```
Or scaffold a starter storyboard from any `.eats` file:
```powershell
python tools/video_director/generate_walkthrough.py --scaffold games/clockwork_dial.eats
```

### Option B: Record Your Own Teaching Moves (`--record-moves`)
Play 10–15 organic teaching moves yourself with live solvability checking on every move:
```powershell
build\Release\solengine.exe --game klondike_draw_1 --seed 26 --record-moves tools\video_director\storyboards\klondike_guided.json
```
Press `[Z]` to Undo if any move warns `UNSOLVABLE`.

---

## 3. Storyboard Best Practices (Avoiding Spoiler / Timing Desyncs)
* **Never name a newly drawn or flipped card in the same beat that reveals it.**
  Split the reveal and the commentary into two beats:
  1. Beat N (`"move_count": 3`): *"Let's turn a few more cards from the Stock until we spot another play."* (reveals the card before Beat N ends).
  2. Beat N+1 (`"move_count": 1`): *"Ah, the Four of Spades! That drops nicely onto the Five of Hearts."* (the card is already visible when the line starts).
* **Use `highlight_zone` for board tours:** Targets full zone groups (e.g., `2` = all Foundations, `6` = all Tableau columns, `0` = Stock/Waste) with spotlighting and magic bursts.
* **End with `finish_game` + `reset_camera`:** `finish_game` solves from the current board state at 4× (`Hyper`) speed until victory, and `reset_camera` (`pad_sec: 3.0`) plays the outro over the 3D victory card cascade.

---

## 4. Generate Narration, Timeline & Subtitles
```powershell
python tools/video_director/generate_walkthrough.py tools/video_director/storyboards/klondike_draw_1.json
```
* Audio clips are cached in `.cache/` by `(voice_id, model_id, text)` hash—editing one beat only re-synthesizes that beat (`$0` for unchanged beats).
* Outputs in `tools/video_director/out/<slug>/`:
  * `timeline.dir` — Beat timeline for `solengine.exe --director`
  * `captions.srt` — Ready-to-upload YouTube closed captions

*(Pass `--dry-run` to test timing with silent placeholder WAVs without spending ElevenLabs credits.)*

---

## 5. Preview Live or Export 1080p60 / 4K60 MP4
* **Live preview:**
  ```powershell
  build\Release\solengine.exe --game klondike_draw_1 --seed 26 --director "C:\git\solengine\tools\video_director\out\klondike_draw_1\timeline.dir" --hide-hud --width 1920 --height 1080
  ```
* **Non-realtime MP4 export** (locked 60 fps, offscreen WebGPU piped directly to `ffmpeg`, sample-accurate offline audio):
  ```powershell
  python tools/video_director/export_mp4.py klondike_draw_1 --seed 26 --width 3840 --height 2160 --fps 60
  ```
  *(Use `--width 1920 --height 1080` for a faster 1080p60 render.)*

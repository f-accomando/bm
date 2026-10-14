#!/usr/bin/env python3
"""
montage8.py — Assemble Episode 8 of Tropic Trail ("The game, played")
According to docs/video/08-gameplay/script.md.

Duration: ~1:35 (95 +- 3 s), 1920x1080 @ 60 fps, H.264 + AAC.

Structure:
  0:00 - 0:08 (8 s)  : Hook (from game.mp4, banner "TROPIC TRAIL: THE GAME")
  0:08 - 0:12 (4 s)  : Title card ("Episode 8 · The game")
  0:12 - 0:26 (~14 s): Game play (title screen + bot run + course clear, from game.mp4)
  0:26 - 0:29 (3 s)  : Card ("HOW IT WAS MADE")
  0:29 - 1:11 (42 s) : 7 clips (6 s each, from ep01.mp4..ep07.mp4, with bottom banner)
  1:11 - 1:20 (9 s)  : Closing (clear screen still under voiceover)
  1:20 - 1:35 (15 s) : End card ("TROPIC TRAIL · bm · github.com/f-accomando/bm")
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LIB_DIR = os.path.join(HERE, "..", "..", "lib")
if os.path.isdir(LIB_DIR):
    sys.path.insert(0, LIB_DIR)

FPS = 60
WIDTH, HEIGHT = 1920, 1080

EPISODE_LABELS = [
    (1, "EPISODE 1 · bm Studio"),
    (2, "EPISODE 2 · bm Pixel"),
    (3, "EPISODE 3 · bm Animator"),
    (4, "EPISODE 4 · bm SDK"),
    (5, "EPISODE 5 · bm Sound"),
    (6, "EPISODE 6 · bm SDK"),
    (7, "EPISODE 7 · bm Code"),
]


def find_binary(name):
    """Find binary executable in PATH or standard system/winget directories."""
    p = shutil.which(name)
    if p:
        return p
    local_app_data = os.environ.get("LOCALAPPDATA", "")
    if local_app_data:
        pattern = os.path.join(local_app_data, "Microsoft", "WinGet", "Packages", "*", "*", "bin", name + ".exe")
        matches = glob.glob(pattern)
        if matches:
            return matches[0]
        link = os.path.join(local_app_data, "Microsoft", "WinGet", "Links", name + ".exe")
        if os.path.exists(link):
            return link
    return name


def get_font_spec(bold=False):
    """Return fontfile specification properly formatted and escaped for ffmpeg drawtext filter."""
    dejavu = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    if os.path.exists(dejavu):
        return f"'{dejavu}'"
    # Windows system fonts
    candidates = [
        ("C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf"),
        ("C:/Windows/Fonts/segoeuib.ttf" if bold else "C:/Windows/Fonts/segoeui.ttf"),
    ]
    for c in candidates:
        if os.path.exists(c):
            escaped = c.replace("\\", "/").replace(":", "\\:")
            return f"'{escaped}'"
    return "sans-serif"


def run_cmd(cmd):
    """Run command with logging."""
    print("+", " ".join(str(c) for c in cmd), file=sys.stderr)
    subprocess.run(cmd, check=True)


def probe_file(ffprobe_bin, path):
    """Probe media file and return streams and format dict."""
    cmd = [
        ffprobe_bin, "-hide_banner", "-v", "error",
        "-show_entries", "format=duration:stream=codec_type,width,height,r_frame_rate,channels",
        "-of", "json", path
    ]
    res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return json.loads(res.stdout)


def make_card(ffmpeg_bin, out_path, seconds, lines, fps=FPS):
    """
    Generate a title card video of `seconds` duration.
    lines: list of (text, fontsize, color_hex, y_pos)
    """
    f_bold = get_font_spec(bold=True)
    f_reg = get_font_spec(bold=False)

    vf_filters = []
    for text, size, color, y in lines:
        t = text.replace(":", "\\:").replace("'", "’")
        font = f_bold if size >= 40 else f_reg
        vf_filters.append(
            f"drawtext=fontfile={font}:text='{t}':fontsize={size}:fontcolor={color}:x=(w-text_w)/2:y={y}"
        )
    fade_out_st = max(0.0, seconds - 0.5)
    vf_filters.append(f"fade=t=in:st=0:d=0.5,fade=t=out:st={fade_out_st:.2f}:d=0.5")

    cmd = [
        ffmpeg_bin, "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", f"color=c=0x0b0d13:s={WIDTH}x{HEIGHT}:r={fps}:d={seconds:.2f}",
        "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo",
        "-vf", ",".join(vf_filters),
        "-shortest",
        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p", "-r", str(fps),
        "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
        out_path
    ]
    run_cmd(cmd)


def make_hook(ffmpeg_bin, ffprobe_bin, game_mp4, out_path, seconds=8.0):
    """
    Segment 1: Hook 8s from game.mp4 with top banner 'TROPIC TRAIL: THE GAME'.
    """
    font = get_font_spec(bold=True)
    info = probe_file(ffprobe_bin, game_mp4)
    has_audio = any(s.get("codec_type") == "audio" for s in info.get("streams", []))

    fade_out_st = max(0.0, seconds - 0.4)
    vf = (
        f"scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=decrease,"
        f"pad={WIDTH}:{HEIGHT}:(ow-iw)/2:(oh-ih)/2:color=0x0b0d13,"
        f"fps={FPS},"
        "drawbox=x=0:y=0:w=1920:h=120:color=0x0b0d13@1:t=fill,"
        f"drawtext=fontfile={font}:text='TROPIC TRAIL\\: THE GAME':fontsize=52:fontcolor=0xffc050:x=(w-text_w)/2:y=34,"
        f"fade=t=in:st=0:d=0.4,fade=t=out:st={fade_out_st:.2f}:d=0.4"
    )

    cmd = [
        ffmpeg_bin, "-hide_banner", "-loglevel", "error", "-y",
        "-ss", "0", "-t", f"{seconds:.2f}",
        "-i", game_mp4
    ]
    if not has_audio:
        cmd += ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo"]
        cmd += ["-filter_complex", f"[0:v]{vf}[v];[1:a]afade=t=in:st=0:d=0.4,afade=t=out:st={fade_out_st:.2f}:d=0.4[a]"]
        cmd += ["-map", "[v]", "-map", "[a]"]
    else:
        af = f"afade=t=in:st=0:d=0.4,afade=t=out:st={fade_out_st:.2f}:d=0.4"
        cmd += ["-vf", vf, "-af", af]

    cmd += [
        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p", "-r", str(FPS),
        "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
        out_path
    ]
    run_cmd(cmd)


def make_game_cut(ffmpeg_bin, ffprobe_bin, game_mp4, out_path, max_seconds=14.0):
    """
    Segment 3: The actual game run (~14 s).
    """
    info = probe_file(ffprobe_bin, game_mp4)
    total_dur = float(info["format"]["duration"])
    dur = min(total_dur, max_seconds)
    has_audio = any(s.get("codec_type") == "audio" for s in info.get("streams", []))

    vf = (
        f"scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=decrease,"
        f"pad={WIDTH}:{HEIGHT}:(ow-iw)/2:(oh-ih)/2:color=0x0b0d13,"
        f"fps={FPS}"
    )

    cmd = [
        ffmpeg_bin, "-hide_banner", "-loglevel", "error", "-y",
        "-ss", "0", "-t", f"{dur:.2f}",
        "-i", game_mp4
    ]
    if not has_audio:
        cmd += ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo"]
        cmd += ["-filter_complex", f"[0:v]{vf}[v];[1:a]aformat=sample_rates=48000:channel_layouts=stereo[a]"]
        cmd += ["-map", "[v]", "-map", "[a]"]
    else:
        cmd += ["-vf", vf, "-af", "aformat=sample_rates=48000:channel_layouts=stereo"]

    cmd += [
        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p", "-r", str(FPS),
        "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
        out_path
    ]
    run_cmd(cmd)


def make_clip(ffmpeg_bin, ffprobe_bin, in_path, out_path, label, seconds=6.0):
    """
    Segments 5-11: 6s clip with bottom banner 'EPISODE n · <tool>'.
    """
    font = get_font_spec(bold=True)
    info = probe_file(ffprobe_bin, in_path)
    has_audio = any(s.get("codec_type") == "audio" for s in info.get("streams", []))

    lbl_esc = label.replace(":", "\\:").replace("'", "’")
    fade_out_st = max(0.0, seconds - 0.3)
    vf = (
        f"scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=decrease,"
        f"pad={WIDTH}:{HEIGHT}:(ow-iw)/2:(oh-ih)/2:color=0x0b0d13,"
        f"fps={FPS},"
        "drawbox=x=0:y=960:w=1920:h=120:color=0x0b0d13@0.85:t=fill,"
        f"drawtext=fontfile={font}:text='{lbl_esc}':fontsize=42:fontcolor=0xffc050:x=(w-text_w)/2:y=996,"
        f"fade=t=in:st=0:d=0.3,fade=t=out:st={fade_out_st:.2f}:d=0.3"
    )

    cmd = [
        ffmpeg_bin, "-hide_banner", "-loglevel", "error", "-y",
        "-ss", "0", "-t", f"{seconds:.2f}",
        "-i", in_path
    ]
    if not has_audio:
        cmd += ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo"]
        cmd += ["-filter_complex", f"[0:v]{vf}[v];[1:a]afade=t=in:st=0:d=0.3,afade=t=out:st={fade_out_st:.2f}:d=0.3[a]"]
        cmd += ["-map", "[v]", "-map", "[a]"]
    else:
        af = f"afade=t=in:st=0:d=0.3,afade=t=out:st={fade_out_st:.2f}:d=0.3,aformat=sample_rates=48000:channel_layouts=stereo"
        cmd += ["-vf", vf, "-af", af]

    cmd += [
        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p", "-r", str(FPS),
        "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
        out_path
    ]
    run_cmd(cmd)


def make_closing(ffmpeg_bin, ffprobe_bin, game_mp4, out_path, seconds=9.0):
    """
    Segment 12: 9s still frame of the end of the game ('COURSE CLEAR') under voiceover.
    """
    info = probe_file(ffprobe_bin, game_mp4)
    total_dur = float(info["format"]["duration"])
    ss_time = max(0.0, total_dur - 1.0)
    fade_out_st = max(0.0, seconds - 0.5)

    vf = (
        f"scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=decrease,"
        f"pad={WIDTH}:{HEIGHT}:(ow-iw)/2:(oh-ih)/2:color=0x0b0d13,"
        f"fps={FPS},"
        f"fade=t=in:st=0:d=0.3,fade=t=out:st={fade_out_st:.2f}:d=0.5"
    )

    cmd = [
        ffmpeg_bin, "-hide_banner", "-loglevel", "error", "-y",
        "-ss", f"{ss_time:.2f}",
        "-i", game_mp4,
        "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo",
        "-filter_complex", f"[0:v]loop=loop=-1:size=1:start=0[v0];[v0]{vf}[v]",
        "-map", "[v]", "-map", "1:a",
        "-t", f"{seconds:.2f}",
        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p", "-r", str(FPS),
        "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
        out_path
    ]
    run_cmd(cmd)


def concat_all(ffmpeg_bin, part_files, final_out):
    """Concatenate all parts together with audio."""
    n = len(part_files)
    cmd = [ffmpeg_bin, "-hide_banner", "-loglevel", "error", "-y"]
    for p in part_files:
        cmd += ["-i", p]
    fc = "".join(f"[{i}:v][{i}:a]" for i in range(n)) + f"concat=n={n}:v=1:a=1[v][a]"
    cmd += [
        "-filter_complex", fc,
        "-map", "[v]", "-map", "[a]",
        "-c:v", "libx264", "-crf", "18", "-preset", "medium", "-pix_fmt", "yuv420p", "-r", str(FPS),
        "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
        "-movflags", "+faststart",
        final_out
    ]
    run_cmd(cmd)


def find_clip_file(clips_dir, ep_num):
    """Locate clip file for episode N with flexible naming."""
    patterns = [
        f"ep{ep_num:02d}.mp4",
        f"ep{ep_num}.mp4",
        f"*{ep_num:02d}*.mp4",
        f"*{ep_num}*.mp4",
    ]
    for pat in patterns:
        matches = glob.glob(os.path.join(clips_dir, pat))
        if matches:
            return matches[0]
    return None


def assemble_montage8(clips_dir, game_mp4, out_path, work_dir=None, keep_temp=False,
                     ffmpeg_bin=None, ffprobe_bin=None):
    """Assemble the complete Episode 8 video."""
    ffmpeg_bin = ffmpeg_bin or find_binary("ffmpeg")
    ffprobe_bin = ffprobe_bin or find_binary("ffprobe")

    if not os.path.exists(game_mp4):
        raise FileNotFoundError(f"game.mp4 not found: {game_mp4}")

    tmp_dir = work_dir or tempfile.mkdtemp(prefix="montage8_")
    os.makedirs(tmp_dir, exist_ok=True)
    parts = []

    try:
        # Part 1: Hook (8.0 s)
        p1 = os.path.join(tmp_dir, "part01_hook.mp4")
        make_hook(ffmpeg_bin, ffprobe_bin, game_mp4, p1, seconds=8.0)
        parts.append(p1)

        # Part 2: Title Card (4.0 s)
        p2 = os.path.join(tmp_dir, "part02_title.mp4")
        make_card(ffmpeg_bin, p2, 4.0, [
            ("Episode 8 · The game", 56, "0xf0e4e0", 460),
            ("TROPIC TRAIL", 36, "0x50c0ff", 560),
        ])
        parts.append(p2)

        # Part 3: Game Play (~14.0 s)
        p3 = os.path.join(tmp_dir, "part03_game.mp4")
        make_game_cut(ffmpeg_bin, ffprobe_bin, game_mp4, p3, max_seconds=14.0)
        parts.append(p3)

        # Part 4: Card 'HOW IT WAS MADE' (3.0 s)
        p4 = os.path.join(tmp_dir, "part04_made.mp4")
        make_card(ffmpeg_bin, p4, 3.0, [
            ("HOW IT WAS MADE", 60, "0xf0e4e0", 500),
            ("TROPIC TRAIL · The 3D series", 32, "0x9098b0", 590),
        ])
        parts.append(p4)

        # Parts 5-11: 7 clips (6.0 s each = 42.0 s)
        for ep_num, label in EPISODE_LABELS:
            clip_src = find_clip_file(clips_dir, ep_num)
            if not clip_src:
                raise FileNotFoundError(f"Clip for episode {ep_num} not found in {clips_dir}")
            p_clip = os.path.join(tmp_dir, f"part{ep_num + 4:02d}_ep{ep_num:02d}.mp4")
            make_clip(ffmpeg_bin, ffprobe_bin, clip_src, p_clip, label, seconds=6.0)
            parts.append(p_clip)

        # Part 12: Closing still screen (9.0 s)
        p12 = os.path.join(tmp_dir, "part12_closing.mp4")
        make_closing(ffmpeg_bin, ffprobe_bin, game_mp4, p12, seconds=9.0)
        parts.append(p12)

        # Part 13: End Card (15.0 s)
        p13 = os.path.join(tmp_dir, "part13_endcard.mp4")
        make_card(ffmpeg_bin, p13, 15.0, [
            ("TROPIC TRAIL", 64, "0xffc050", 400),
            ("bm · the bare-metal fantasy console", 38, "0xe0e4f0", 500),
            ("github.com/f-accomando/bm", 34, "0x50c0ff", 580),
        ])
        parts.append(p13)

        # Final concatenation
        concat_all(ffmpeg_bin, parts, out_path)

        # Verify output
        info = probe_file(ffprobe_bin, out_path)
        dur = float(info["format"]["duration"])
        v_streams = [s for s in info["streams"] if s["codec_type"] == "video"]
        a_streams = [s for s in info["streams"] if s["codec_type"] == "audio"]
        assert v_streams, "Missing video stream in output"
        assert a_streams, "Missing audio stream in output"
        w, h = int(v_streams[0]["width"]), int(v_streams[0]["height"])
        assert w == WIDTH and h == HEIGHT, f"Output dimensions {w}x{h} != {WIDTH}x{HEIGHT}"
        print(f"Montage completed successfully: {out_path} ({dur:.2f} s, {w}x{h} @ 60fps)")
        return dur

    finally:
        if not keep_temp and work_dir is None:
            shutil.rmtree(tmp_dir, ignore_errors=True)


def run_self_test(ffmpeg_bin=None, ffprobe_bin=None):
    """Self-test generating synthetic clips and assembling a test montage."""
    ffmpeg_bin = ffmpeg_bin or find_binary("ffmpeg")
    ffprobe_bin = ffprobe_bin or find_binary("ffprobe")

    test_dir = tempfile.mkdtemp(prefix="montage8_test_")
    print(f"Creating synthetic test environment in {test_dir}...")
    try:
        # Generate synthetic game.mp4 (15.0 s)
        game_path = os.path.join(test_dir, "game.mp4")
        cmd_game = [
            ffmpeg_bin, "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "testsrc=size=1920x1080:rate=60:duration=15",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=15",
            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-r", "60",
            "-c:a", "aac", "-b:a", "128k", "-ar", "48000", "-ac", "2",
            game_path
        ]
        run_cmd(cmd_game)

        # Generate 7 synthetic episode clips (10.0 s each)
        clips_dir = os.path.join(test_dir, "clips")
        os.makedirs(clips_dir, exist_ok=True)
        for ep_num in range(1, 8):
            clip_path = os.path.join(clips_dir, f"ep{ep_num:02d}.mp4")
            cmd_clip = [
                ffmpeg_bin, "-hide_banner", "-loglevel", "error", "-y",
                "-f", "lavfi", "-i", f"testsrc=size=1920x1080:rate=60:duration=10",
                "-f", "lavfi", "-i", f"sine=frequency={300 + ep_num * 50}:duration=10",
                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-r", "60",
                "-c:a", "aac", "-b:a", "128k", "-ar", "48000", "-ac", "2",
                clip_path
            ]
            run_cmd(cmd_clip)

        # Assemble test output
        out_path = os.path.join(test_dir, "test_ep08.mp4")
        dur = assemble_montage8(
            clips_dir=clips_dir,
            game_mp4=game_path,
            out_path=out_path,
            ffmpeg_bin=ffmpeg_bin,
            ffprobe_bin=ffprobe_bin
        )

        assert 92.0 <= dur <= 98.0, f"Duration {dur:.2f} s outside expected 95 +- 3 s"
        print(f"SELF-TEST ALL OK: duration {dur:.2f} s (target 95 +- 3 s)")
        return 0
    finally:
        shutil.rmtree(test_dir, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description="Assemble Episode 8 video for Tropic Trail")
    parser.add_argument("--clips", metavar="DIR", help="Directory containing ep01.mp4..ep07.mp4")
    parser.add_argument("--game", metavar="PATH", help="Path to game.mp4")
    parser.add_argument("-o", "--out", default="ep08.mp4", help="Output video path (default: ep08.mp4)")
    parser.add_argument("--work-dir", metavar="DIR", help="Temporary working directory")
    parser.add_argument("--keep-temp", action="store_true", help="Keep temporary cut files")
    parser.add_argument("--test", action="store_true", help="Run synthetic self-test")
    parser.add_argument("--ffmpeg", help="Custom ffmpeg path")
    parser.add_argument("--ffprobe", help="Custom ffprobe path")

    args = parser.parse_args()

    ffmpeg_bin = find_binary(args.ffmpeg) if args.ffmpeg else find_binary("ffmpeg")
    ffprobe_bin = find_binary(args.ffprobe) if args.ffprobe else find_binary("ffprobe")

    if args.test:
        return run_self_test(ffmpeg_bin, ffprobe_bin)

    if not args.clips or not args.game:
        parser.error("--clips DIR and --game PATH are required (or use --test for self-test)")

    assemble_montage8(
        clips_dir=args.clips,
        game_mp4=args.game,
        out_path=args.out,
        work_dir=args.work_dir,
        keep_temp=args.keep_temp,
        ffmpeg_bin=ffmpeg_bin,
        ffprobe_bin=ffprobe_bin
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Tropic Trail, episode 7: montage of the final video (docs/video/07-code/).

Usage:
  python3 video/3d/07-code/montage7.py [--test] [--r1 PATH] [--r2 PATH] [--r3 PATH] [-o OUT.mp4]

Structure (docs/video/07-code/script.md, ~4:04):
  0:00 - 0:08 ( 8 s) : Hook (clip of finished game R2 with top banner)
  0:08 - 0:11 ( 3 s) : Title card ("Episode 7 · Code the game", silent)
  0:11 - 3:36 (205 s) : R1 (bm Code recording, cut 205 s)
  3:36 - 3:42 ( 6 s) : R3 (bm SDK recording, cut ss=8, 6 s, hiding Ctrl+O/Open)
  3:42 - 4:02 (20 s) : R2 (gameplay with bottom action subtitles)
Total: 242 s (~4:04), 1920x1080 @ 60 fps, H.264 + stereo AAC.

Cross-platform: works on Linux and Windows (locates ffmpeg/ffprobe, resolves fonts).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

FPS = 60
W, H = 480, 270

HOOK_SECONDS = 8
INTRO_SECONDS = 3
R1_SECONDS = 205
R3_SECONDS = 6
R3_SS = 8
R2_SECONDS = 20

TARGET_TOTAL = HOOK_SECONDS + INTRO_SECONDS + R1_SECONDS + R3_SECONDS + R2_SECONDS  # 242 s (~4:04)

PART_SPEC = [
    ("hook.mp4", HOOK_SECONDS),
    ("intro.mp4", INTRO_SECONDS),
    ("r1.mp4", R1_SECONDS),
    ("r3.mp4", R3_SECONDS),
    ("r2.mp4", R2_SECONDS),
]

INTRO_LINES = [
    ("TROPIC TRAIL", 96, "0xffc050", 380),
    ("Episode 7  ·  bm Code", 56, "0xe0e4f0", 520),
    ("Code the game", 34, "0x9098b0", 640),
]

R2_LINES = [
    (0, 2, "A / Space: start"),
    (2, 6, "← ↑ → ↓: run"),
    (6, 11, "B / X: spin"),
    (11, 16, "A / Space: jump (hold: higher)"),
    (16, 20, "Next: the whole trail"),
]

ASS_HEAD = """[Script Info]
ScriptType: v4.00+
PlayResX: 1920
PlayResY: 1080
WrapStyle: 0
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,Alignment,MarginL,MarginR,MarginV,Encoding
Style: Say,DejaVu Sans,40,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,3,0,2,60,60,0,1

[Events]
Format: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text
"""


def find_binary(name):
    p = shutil.which(name)
    if p:
        return p
    winget_dir = os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Packages")
    if os.path.isdir(winget_dir):
        for root, dirs, files in os.walk(winget_dir):
            if f"{name}.exe" in files:
                return os.path.join(root, f"{name}.exe")
    links_dir = os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Links")
    candidate = os.path.join(links_dir, f"{name}.exe")
    if os.path.isfile(candidate):
        return candidate
    for prog in [os.path.expandvars(r"%ProgramFiles%"), os.path.expandvars(r"%ProgramFiles(x86)%")]:
        candidate = os.path.join(prog, name, "bin", f"{name}.exe")
        if os.path.isfile(candidate):
            return candidate
    return name


FFMPEG = find_binary("ffmpeg")
FFPROBE = find_binary("ffprobe")


def get_font_spec(bold=True):
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/TTF/DejaVuSans.ttf",
        os.path.expandvars(r"C:\Windows\Fonts\arialbd.ttf") if bold else os.path.expandvars(r"C:\Windows\Fonts\arial.ttf"),
        os.path.expandvars(r"C:\Windows\Fonts\segoeuib.ttf") if bold else os.path.expandvars(r"C:\Windows\Fonts\segoeui.ttf"),
    ]
    for c in candidates:
        if os.path.exists(c):
            escaped = c.replace("\\", "/").replace(":", r"\:")
            return f"fontfile='{escaped}'"
    return "font='DejaVu Sans:style=Bold'" if bold else "font='DejaVu Sans'"


def ass_time(f, fps=FPS):
    cs = int(round(f * 100 / fps))
    s = cs // 100
    cs %= 100
    m = s // 60
    s %= 60
    h = m // 60
    m %= 60
    return "%d:%02d:%02d.%02d" % (h, m, s, cs)


def esc_ass(text):
    return text.replace("\\", "\\\\").replace("{", "\\{").replace("}", "\\}")


def write_r2_ass(path):
    with open(path, "w", encoding="utf-8") as f:
        f.write(ASS_HEAD)
        for a, b, text in R2_LINES:
            f.write(
                "Dialogue: 3,%s,%s,Say,,0,0,0,,{\\q0\\an2\\pos(960,1040)}%s\n"
                % (ass_time(a * FPS), ass_time(b * FPS), esc_ass(text))
            )


def run_cmd(cmd, description=""):
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        cmd_str = " ".join(cmd[:6])
        err_msg = res.stderr[-500:].strip() if res.stderr else res.stdout[-500:].strip()
        print(f"Error during {description or cmd_str}:\n{err_msg}", file=sys.stderr)
        raise subprocess.CalledProcessError(res.returncode, cmd, res.stdout, res.stderr)
    return res


def probe(path):
    """(width, height, r_frame_rate, duration, has_audio) or None."""
    if not os.path.isfile(path):
        return None
    cmd = [
        FFPROBE, "-v", "error",
        "-show_entries", "stream=width,height,r_frame_rate,codec_type:format=duration",
        "-of", "json", path
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        return None
    try:
        d = json.loads(res.stdout)
        dur = float(d["format"]["duration"])
        w, h, rate = 0, 0, ""
        has_audio = False
        for s in d.get("streams", []):
            if s.get("codec_type") == "video" and w == 0:
                w = int(s.get("width", 0))
                h = int(s.get("height", 0))
                rate = s.get("r_frame_rate", "")
            elif s.get("codec_type") == "audio":
                has_audio = True
        return w, h, rate, dur, has_audio
    except Exception:
        return None


def card_cmd(out, seconds, lines, fps=FPS):
    """Render a title card with centered text and audio silence."""
    font_bold = get_font_spec(bold=True)
    font_reg = get_font_spec(bold=False)
    vf = []
    for text, size, colour, y in lines:
        t = text.replace(":", r"\:").replace("'", "’")
        fspec = font_bold if size > 40 else font_reg
        vf.append(f"drawtext={fspec}:text='{t}':fontsize={size}:fontcolor={colour}:x=(w-text_w)/2:y={y}")
    vf.append(f"fade=t=in:st=0:d=0.5,fade=t=out:st={seconds - 0.5:.2f}:d=0.5")

    cmd = [
        FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", f"color=c=0x0b0d13:s=1920x1080:r={fps}:d={seconds:.2f}",
        "-f", "lavfi", "-i", f"anullsrc=r=48000:cl=stereo:d={seconds:.2f}",
        "-vf", ",".join(vf),
        "-c:v", "libx264", "-crf", "19", "-preset", "fast", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-b:a", "192k", "-ar", "48000", "-ac", "2",
        "-t", f"{seconds:.2f}",
        out
    ]
    return cmd


def hook_cmd(src, out, seconds=HOOK_SECONDS, start=0.0, banner_text="TROPIC TRAIL · EP 7 · bm Code"):
    """Render hook with top banner box and fade."""
    font_bold = get_font_spec(bold=True)
    t = banner_text.replace(":", r"\:").replace("'", "’")
    vf = (
        f"scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2,"
        f"drawbox=x=0:y=0:w=1920:h=120:color=0x0b0d13@1:t=fill,"
        f"drawtext={font_bold}:text='{t}':fontsize=52:fontcolor=0xffc050:x=(w-text_w)/2:y=34,"
        f"fade=t=in:st=0:d=0.4,fade=t=out:st={seconds - 0.4:.2f}:d=0.4"
    )
    cmd = [
        FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
        "-ss", f"{start:.2f}",
        "-i", src,
        "-f", "lavfi", "-i", f"anullsrc=r=48000:cl=stereo:d={seconds:.2f}",
        "-filter_complex", f"[0:v]{vf}[v];[0:a]aresample=48000,aformat=channel_layouts=stereo[a1];[a1][1:a]amix=inputs=2:duration=first[a]",
        "-map", "[v]", "-map", "[a]",
        "-t", f"{seconds:.2f}",
        "-r", str(FPS),
        "-c:v", "libx264", "-crf", "19", "-preset", "fast", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-b:a", "192k",
        out
    ]
    # Fallback if src has no audio stream
    p = probe(src)
    if p and not p[4]:
        cmd = [
            FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
            "-ss", f"{start:.2f}",
            "-i", src,
            "-f", "lavfi", "-i", f"anullsrc=r=48000:cl=stereo:d={seconds:.2f}",
            "-vf", vf,
            "-map", "0:v", "-map", "1:a",
            "-t", f"{seconds:.2f}",
            "-r", str(FPS),
            "-c:v", "libx264", "-crf", "19", "-preset", "fast", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "192k",
            out
        ]
    return cmd


def cut_cmd(src, out, seconds, ss=0.0):
    """Normalize clip to 1920x1080 @ 60fps with stereo 48000Hz audio."""
    p = probe(src)
    has_audio = p[4] if p else False

    vf = "scale=1920:1080:flags=neighbor,fps=60"

    if has_audio:
        cmd = [
            FFMPEG, "-hide_banner", "-loglevel", "error", "-y"
        ]
        if ss > 0:
            cmd += ["-ss", f"{ss:.2f}"]
        cmd += [
            "-i", src,
            "-t", f"{seconds:.2f}",
            "-vf", vf,
            "-c:v", "libx264", "-crf", "19", "-preset", "fast", "-pix_fmt", "yuv420p",
            "-af", "aresample=48000,aformat=channel_layouts=stereo",
            "-c:a", "aac", "-b:a", "192k",
            out
        ]
    else:
        cmd = [
            FFMPEG, "-hide_banner", "-loglevel", "error", "-y"
        ]
        if ss > 0:
            cmd += ["-ss", f"{ss:.2f}"]
        cmd += [
            "-i", src,
            "-f", "lavfi", "-i", f"anullsrc=r=48000:cl=stereo:d={seconds:.2f}",
            "-t", f"{seconds:.2f}",
            "-vf", vf,
            "-map", "0:v", "-map", "1:a",
            "-c:v", "libx264", "-crf", "19", "-preset", "fast", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "192k",
            out
        ]
    return cmd


def r2_with_subtitles(src, ass_path, out, seconds=R2_SECONDS):
    """Normalize R2 and burn ASS subtitles if available."""
    p = probe(src)
    has_audio = p[4] if p else False

    # Escape subtitle path for ffmpeg filtergraph
    sub_esc = ass_path.replace("\\", "/").replace(":", r"\:")
    vf = f"scale=1920:1080:flags=neighbor,subtitles='{sub_esc}',fps=60"

    if has_audio:
        cmd = [
            FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
            "-i", src,
            "-t", f"{seconds:.2f}",
            "-vf", vf,
            "-c:v", "libx264", "-crf", "19", "-preset", "fast", "-pix_fmt", "yuv420p",
            "-af", "aresample=48000,aformat=channel_layouts=stereo",
            "-c:a", "aac", "-b:a", "192k",
            out
        ]
    else:
        cmd = [
            FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
            "-i", src,
            "-f", "lavfi", "-i", f"anullsrc=r=48000:cl=stereo:d={seconds:.2f}",
            "-t", f"{seconds:.2f}",
            "-vf", vf,
            "-map", "0:v", "-map", "1:a",
            "-c:v", "libx264", "-crf", "19", "-preset", "fast", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "192k",
            out
        ]
    return cmd


def assemble_parts(r1_src, r2_src, r3_src, parts_dir, r2_clean=None):
    os.makedirs(parts_dir, exist_ok=True)
    hook_out = os.path.join(parts_dir, "hook.mp4")
    intro_out = os.path.join(parts_dir, "intro.mp4")
    r1_out = os.path.join(parts_dir, "r1.mp4")
    r3_out = os.path.join(parts_dir, "r3.mp4")
    r2_out = os.path.join(parts_dir, "r2.mp4")

    # 1. Hook (8s) from R2 clean
    clean_src = r2_clean if (r2_clean and os.path.isfile(r2_clean)) else r2_src
    print("Building part 1/5: hook.mp4 (8s)...")
    run_cmd(hook_cmd(clean_src, hook_out, seconds=HOOK_SECONDS, start=0.0), "hook")

    # 2. Intro Title Card (3s)
    print("Building part 2/5: intro.mp4 (3s)...")
    run_cmd(card_cmd(intro_out, INTRO_SECONDS, INTRO_LINES), "intro card")

    # 3. R1 bm Code (205s)
    print("Building part 3/5: r1.mp4 (205s)...")
    run_cmd(cut_cmd(r1_src, r1_out, seconds=R1_SECONDS, ss=0.0), "cut r1")

    # 4. R3 bm SDK (6s, ss=8 hiding Ctrl+O/Open)
    print("Building part 4/5: r3.mp4 (6s)...")
    run_cmd(cut_cmd(r3_src, r3_out, seconds=R3_SECONDS, ss=R3_SS), "cut r3")

    # 5. R2 game (20s) with subtitles
    print("Building part 5/5: r2.mp4 (20s with captions)...")
    ass_path = os.path.join(parts_dir, "r2.ass")
    write_r2_ass(ass_path)
    try:
        run_cmd(r2_with_subtitles(r2_src, ass_path, r2_out, seconds=R2_SECONDS), "r2 with subtitles")
    except Exception as e:
        print(f"Notice: subtitle burn failed ({e}), falling back to standard cut", file=sys.stderr)
        run_cmd(cut_cmd(r2_src, r2_out, seconds=R2_SECONDS, ss=0.0), "cut r2 fallback")


def verify_parts(parts_dir):
    print("\nVerifying parts:")
    for name, expected_dur in PART_SPEC:
        p = os.path.join(parts_dir, name)
        info = probe(p)
        if not info:
            raise RuntimeError(f"Part {name} missing or unreadable: {p}")
        w, h, rate, dur, has_audio = info
        if (w, h) != (1920, 1080):
            raise RuntimeError(f"{name}: invalid resolution {w}x{h}, want 1920x1080")
        if abs(dur - expected_dur) > 0.3:
            raise RuntimeError(f"{name}: invalid duration {dur:.2f}s, want {expected_dur:.2f}s")
        if not has_audio:
            raise RuntimeError(f"{name}: missing audio stream")
        print(f"  {name:10s} -> {w}x{h} @ {rate} fps, duration: {dur:.2f}s, audio: OK")


def concat_all(parts_dir, out_mp4):
    parts = [os.path.join(parts_dir, name) for name, _ in PART_SPEC]
    n = len(parts)
    inputs = []
    for p in parts:
        inputs.extend(["-i", p])

    filter_complex = "".join(f"[{i}:v][{i}:a]" for i in range(n)) + f"concat=n={n}:v=1:a=1[v][a]"
    cmd = [
        FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
        *inputs,
        "-filter_complex", filter_complex,
        "-map", "[v]", "-map", "[a]",
        "-c:v", "libx264", "-crf", "19", "-preset", "medium", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-b:a", "192k", "-ar", "48000",
        "-movflags", "+faststart",
        out_mp4
    ]
    print(f"\nConcatenating {n} parts into {out_mp4}...")
    run_cmd(cmd, "concat parts")
    print(f"Successfully generated: {out_mp4}")


def run_self_test():
    """Generates synthetic source clips and verifies the entire montage pipeline."""
    print("=== RUNNING MONTAGE7 SELF-TEST ===")
    tmpdir = tempfile.mkdtemp(prefix="bm_montage7_test_")
    try:
        r1_test = os.path.join(tmpdir, "test_r1.mp4")
        r2_test = os.path.join(tmpdir, "test_r2.mp4")
        r3_test = os.path.join(tmpdir, "test_r3.mp4")
        parts_dir = os.path.join(tmpdir, "parts")
        out_mp4 = os.path.join(tmpdir, "test_ep07.mp4")

        print("1. Generating synthetic R1 (bm Code, 205s)...")
        # Generate 205s testsrc with sine audio
        run_cmd([
            FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", f"testsrc=size=480x270:rate=60:duration={R1_SECONDS}",
            "-f", "lavfi", "-i", f"sine=frequency=440:sample_rate=48000:duration={R1_SECONDS}",
            "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "128k",
            r1_test
        ], "generate test R1")

        print("2. Generating synthetic R3 (bm SDK, 14s)...")
        # Generate 14s testsrc
        run_cmd([
            FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "testsrc=size=480x270:rate=60:duration=14",
            "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo:duration=14",
            "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "128k",
            r3_test
        ], "generate test R3")

        print("3. Generating synthetic R2 (Game, 20s)...")
        # Generate 20s testsrc with sound
        run_cmd([
            FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", f"testsrc=size=480x270:rate=60:duration={R2_SECONDS}",
            "-f", "lavfi", "-i", f"sine=frequency=880:sample_rate=48000:duration={R2_SECONDS}",
            "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "128k",
            r2_test
        ], "generate test R2")

        print("4. Assembling parts...")
        assemble_parts(r1_test, r2_test, r3_test, parts_dir)

        print("5. Verifying parts...")
        verify_parts(parts_dir)

        print("6. Concatenating final video...")
        concat_all(parts_dir, out_mp4)

        print("7. Probing output video...")
        info = probe(out_mp4)
        if not info:
            raise RuntimeError("Final test_ep07.mp4 could not be probed")
        w, h, rate, dur, has_audio = info
        print(f"Output probe: {w}x{h} @ {rate} fps, duration {dur:.2f}s, audio: {has_audio}")

        if (w, h) != (1920, 1080):
            raise AssertionError(f"Resolution {w}x{h} != 1920x1080")
        if not has_audio:
            raise AssertionError("Output has no audio stream")
        if abs(dur - TARGET_TOTAL) > 3.0:
            raise AssertionError(f"Duration {dur:.2f}s out of tolerance (target {TARGET_TOTAL} +- 3s)")

        print(f"\n=== SELF-TEST ALL OK: duration {dur:.2f} s (target {TARGET_TOTAL} +- 3 s), 1920x1080 @ 60fps, stereo AAC ===")
        return 0
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description="Assemble Episode 7 video from R1, R2, R3 recordings.")
    parser.add_argument("--test", action="store_true", help="Run self-test with synthetic testsrc clips")
    parser.add_argument("--r1", default=os.path.join(HERE, "out", "qemu-r1", "ep07_qemu_r1.mp4"),
                        help="Path to R1 recording (bm Code, ~205 s)")
    parser.add_argument("--r2", default=os.path.join(HERE, "out", "r2.mp4"),
                        help="Path to R2 recording (gameplay, ~20 s)")
    parser.add_argument("--r2-clean", default=os.path.join(HERE, "out", "r2_clean.mp4"),
                        help="Path to clean R2 recording without captions (optional, for hook)")
    parser.add_argument("--r3", default=os.path.join(HERE, "out", "qemu-r3", "ep07_qemu_r3.mp4"),
                        help="Path to R3 recording (SDK build, ~14 s)")
    parser.add_argument("-o", "--out", default=os.path.join(HERE, "out", "ep07.mp4"),
                        help="Output final mp4 path (default: out/ep07.mp4)")

    args = parser.parse_args()

    if args.test:
        return run_self_test()

    for path, label in [(args.r1, "R1"), (args.r2, "R2"), (args.r3, "R3")]:
        if not os.path.isfile(path):
            print(f"Error: missing {label} input file: {path}", file=sys.stderr)
            print("Tip: use --test to run the synthetic verification self-test.", file=sys.stderr)
            return 1

    out_dir = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(out_dir, exist_ok=True)
    parts_dir = os.path.join(out_dir, "parts7")

    assemble_parts(args.r1, args.r2, args.r3, parts_dir, r2_clean=args.r2_clean)
    verify_parts(parts_dir)
    concat_all(parts_dir, args.out)

    info = probe(args.out)
    if info:
        w, h, rate, dur, audio = info
        print(f"\nAssembly complete! {args.out}: {w}x{h} @ {rate} fps, duration {dur:.2f} s, audio: {audio}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

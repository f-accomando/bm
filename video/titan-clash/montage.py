import os, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from bmvideo import card_cmd, FONT, FONT_R
def run(c): print("+", c[0], c[-1]); subprocess.run(c, check=True)
A = ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo"]
AF = ["-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2"]
V = ["-c:v", "libx264", "-crf", "17", "-preset", "medium", "-pix_fmt", "yuv420p", "-r", "60"]
up = "scale=1920:1080:flags=neighbor"
run(card_cmd("c1.mp4", 3.5, [("TITAN CLASH", 110, "0xffc050", 400), ("CPU vs CPU: from boot to the first K.O.", 44, "0xe0e4f0", 560),
                             ("bm, the bare-metal console for Raspberry Pi Zero W", 32, "0x9098b0", 660)], silent=True))
run(["ffmpeg","-v","error","-y","-ss","1.3","-t","15.2","-i","raw.mp4",*A,"-vf",up+",fps=60,fade=t=in:st=0:d=0.4","-shortest",*V,*AF,"c2.mp4"])
run(["ffmpeg","-v","error","-y","-t","29.8","-i","match_v.mp4","-t","29.8","-i","match.wav","-vf",up+",fade=t=out:st=29.3:d=0.5",
     "-af","afade=t=out:st=29.3:d=0.5",*V,*AF,"c3.mp4"])
run(card_cmd("c4.mp4", 4.5, [("TITAN CLASH", 96, "0xffc050", 380), ("Made for bm: the bare-metal fantasy console", 40, "0xe0e4f0", 520),
                             ("github.com/f-accomando/bm", 40, "0x9098b0", 600)], silent=True))
parts = ["c1.mp4","c2.mp4","c3.mp4","c4.mp4"]
cmd = ["ffmpeg","-v","error","-y"]
for p in parts: cmd += ["-i", p]
fc = "".join(f"[{i}:v][{i}:a]" for i in range(4)) + "concat=n=4:v=1:a=1[v][a]"
run(cmd + ["-filter_complex", fc, "-map","[v]","-map","[a]", *V, *AF, "-movflags","+faststart","titan-clash-cpu-vs-cpu.mp4"])

#!/usr/bin/env python3
"""make test-agent: carts/agent in bmhost with a fake model (tests/agent/prelude.lua before main.lua).

  run_agent_test.py BMHOST MKBM_PY BUILD_DIR

It types two questions, says yes to the proposed file, and checks: the file is in bm/web with
the model's bytes, the second question sent the history with the model's raw file block, the
chat's screenshots are made, and the cart says it is not set up without ai_url.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
host, mkbm, out = sys.argv[1], sys.argv[2], sys.argv[3]
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += not ok


shutil.rmtree(out, ignore_errors=True)
os.makedirs(os.path.join(out, "sd"))
lua = os.path.join(out, "agent_test.lua")
with open(lua, "w") as f:
    f.write(open(os.path.join(HERE, "prelude.lua")).read())
    f.write("\n")
    f.write(open(os.path.join(ROOT, "carts", "agent", "main.lua")).read())
cart = os.path.join(out, "agent_test.bm")
subprocess.run([sys.executable, mkbm, "-o", cart, "--lua", lua, "--info", os.path.join(ROOT, "carts", "agent", "bm.info")],
               check=True, stdout=subprocess.DEVNULL)
script = os.path.join(out, "input.txt")
with open(script, "w") as f:
    f.write("10 source keyboard\n20 type make a file please\\n\n40 shot proposal\n50 type y\n"
            "90 type and now\\n\n120 shot chat\n130 quit\n")
r = subprocess.run([host, cart, "--sd", os.path.join(out, "sd"), "--seconds", "3", "--input", script,
                    "--shots", out], capture_output=True, text=True)
log = r.stdout + r.stderr
check(r.returncode == 0 and "ok," in log, "the cart runs without a Lua error")
saved = os.path.join(out, "sd", "bm", "web", "hello.lua")
check(os.path.exists(saved) and open(saved).read() == 'function _draw()\n  cls()\n  print("hi", 8, 8)\nend\n',
      "the proposed file is in bm/web with the model's bytes after y")
check("fake: ask 1, 1 messages, system yes, model nil, raw false" in log, "the first question: one message and the system text")
check("fake: ask 2, 3 messages" in log and "raw true" in log, "the second question sends the history with the raw file block")
check(all(os.path.exists(os.path.join(out, n + ".png")) for n in ("proposal", "chat")), "screenshots proposal.png and chat.png")

# the wizard: not set up at the start; Groq with a key, then /setup again and the relay by its code
wiz = os.path.join(out, "agent_wizard.lua")
with open(wiz, "w") as f:
    f.write("FAKE_UNSET = true\n")
    f.write(open(os.path.join(HERE, "prelude.lua")).read())
    f.write("\n")
    f.write(open(os.path.join(ROOT, "carts", "agent", "main.lua")).read())
wcart = os.path.join(out, "agent_wizard.bm")
subprocess.run([sys.executable, mkbm, "-o", wcart, "--lua", wiz, "--info", os.path.join(ROOT, "carts", "agent", "bm.info")],
               check=True, stdout=subprocess.DEVNULL)
with open(script, "w") as f:
    f.write("10 source keyboard\n15 shot wizard\n20 type 1\\n\n30 type gsk_secret_key_9876\n35 shot key\n"
            "40 type \\n\n50 type \\n\n80 shot tested\n"
            "100 type /setup\\n\n110 type 5\\n\n120 type 192.168.1.20\\n\n"
            "130 type 7k3m-q9tb-xw2d-h4pa-r8nf\\n\n170 shot paired\n180 quit\n")
r = subprocess.run([host, wcart, "--sd", os.path.join(out, "sd"), "--seconds", "4", "--input", script, "--shots", out],
                   capture_output=True, text=True)
log = r.stdout + r.stderr
check(r.returncode == 0 and "ok," in log, "the wizard runs without a Lua error")
check("fake: ai_setup url https://api.groq.com/openai/v1 model openai/gpt-oss-20b key ...9876" in log,
      "Groq: ai_setup gets the preset's address, the default model and the typed key")
check("gsk_secret_key" not in log, "the key is not in the log")
check("fake: ask 1, 1 messages, system no" in log, "after saving, one test question")
check("fake: ai_pair 192.168.1.20 7k3m-q9tb-xw2d-h4pa-r8nf" in log and "fake: ai_pair_finish 43" in log,
      "relay: ai_pair with the address and the code, then ai_pair_finish")
check(all(os.path.exists(os.path.join(out, n + ".png")) for n in ("wizard", "key", "tested", "paired")),
      "screenshots wizard, key, tested, paired")

real = os.path.join(out, "agent_real.bm")
shutil.copy(os.path.join(ROOT, "build", "carts", "agent.bm") if os.path.exists(os.path.join(ROOT, "build", "carts", "agent.bm")) else cart, real)
with open(script, "w") as f:
    f.write("20 shot unset\n30 quit\n")
env = dict(os.environ, BMHOST_CONFIG="")
r = subprocess.run([host, real, "--sd", os.path.join(out, "sd"), "--seconds", "1", "--input", script, "--shots", out],
                   capture_output=True, text=True, env=env)
check(r.returncode == 0 and os.path.exists(os.path.join(out, "unset.png")), "without ai_url the real cart starts and says so")
print("agent: all passed" if not fails else "agent: %d FAILED" % fails)
sys.exit(1 if fails else 0)

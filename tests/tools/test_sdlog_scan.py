"""Tests for scripts/sdlog_scan.py."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "sdlog_scan.py")

CLEAN_BOOTPREV = """[0.000] boot: starting
--- the run before ended cleanly, or the power went (no record in RAM); the last lines it printed (RAM) ---
[12.345] main loop done
"""

UNCLEAN_BOOTPREV = """[0.000] boot: starting
--- the run before ended without a clean exit (a freeze, or a restart) while: menu, 42 s after boot, kernel abc1234; the last lines it printed (RAM) ---
[42.000] menu tick
"""

CRASH_BOOTPREV = """[0.000] boot: starting
--- the run before ended in a crash while: game loop, 15 s after boot, kernel abc1234; the last lines it printed (RAM) ---
[15.123] crash at 0x12345
"""

CLEAN_BOOTLOG = """[0.000] boot: kernel starting
[0.010] watchdog: on, 3000 ms
[0.020] ready
"""


class TestSdlogScan(unittest.TestCase):
    def run_scan(self, files_dict, extra_args=None, use_bm_subfolder=False):
        with tempfile.TemporaryDirectory() as tmp:
            target_dir = os.path.join(tmp, "bm") if use_bm_subfolder else tmp
            os.makedirs(target_dir, exist_ok=True)
            for fname, content in files_dict.items():
                with open(os.path.join(target_dir, fname), "w", encoding="utf-8") as f:
                    f.write(content)
            cmd = [sys.executable, SCRIPT, tmp if use_bm_subfolder else target_dir]
            if extra_args:
                cmd.extend(extra_args)
            return subprocess.run(cmd, capture_output=True, text=True)

    def test_clean_exit_0(self):
        files = {
            "bootprev.txt": CLEAN_BOOTPREV,
            "bootlog.txt": CLEAN_BOOTLOG,
            "lastrun.txt": "lastrun line 1\nlastrun line 2\n",
        }
        res = self.run_scan(files)
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertIn("bootprev: CLEAN ended cleanly", res.stdout)
        self.assertIn("bootlog: watchdog ON; previous run -", res.stdout)
        self.assertIn("lastrun: PRESENT (2 lines)", res.stdout)
        self.assertIn("verdict: OK", res.stdout)

    def test_unclean_exit_1_freeze(self):
        files = {
            "bootprev.txt": UNCLEAN_BOOTPREV,
            "bootlog.txt": CLEAN_BOOTLOG,
            "lastrun.txt": "restarting log\n",
        }
        res = self.run_scan(files)
        self.assertEqual(res.returncode, 1)
        self.assertIn("bootprev: UNCLEAN ended without a clean exit", res.stdout)
        self.assertIn("verdict: FREEZE", res.stdout)

    def test_crash_exit_1_crash(self):
        files = {
            "bootprev.txt": CRASH_BOOTPREV,
            "bootlog.txt": "[0.010] watchdog: on, 3000 ms\n[0.020] the last run ended in a crash: saved as a report\n",
            "lastrun.txt": "log\n",
        }
        res = self.run_scan(files)
        self.assertEqual(res.returncode, 1)
        self.assertIn("bootprev: CRASH ended in a crash", res.stdout)
        self.assertIn("previous run CRASHED", res.stdout)
        self.assertIn("verdict: CRASH", res.stdout)

    def test_bootprev_clean_but_bootlog_freeze_exit_1_freeze(self):
        bootlog = "[0.010] watchdog: on\n\x1b[91mthe Pi froze last time and the watchdog restarted it:\x1b[0m\n"
        files = {
            "bootprev.txt": CLEAN_BOOTPREV,
            "bootlog.txt": bootlog,
            "lastrun.txt": "log\n",
        }
        res = self.run_scan(files)
        self.assertEqual(res.returncode, 1)
        self.assertIn("previous run FROZE", res.stdout)
        self.assertIn("verdict: FREEZE", res.stdout)

    def test_bootprev_missing_exit_1_incomplete(self):
        files = {
            "bootlog.txt": CLEAN_BOOTLOG,
            "lastrun.txt": "log\n",
        }
        res = self.run_scan(files)
        self.assertEqual(res.returncode, 1)
        self.assertIn("bootprev: MISSING", res.stdout)
        self.assertIn("verdict: INCOMPLETE", res.stdout)

    def test_uppercase_names_and_bm_subfolder(self):
        # cards of before the lowercase rule: the scan reads in any case
        files = {
            "BOOTPREV.TXT": CLEAN_BOOTPREV,
            "BOOTLOG.TXT": CLEAN_BOOTLOG,
            "LASTRUN.TXT": "log line" + chr(10),
        }
        res = self.run_scan(files, use_bm_subfolder=True)
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertIn("bootprev: CLEAN", res.stdout)
        self.assertIn("verdict: OK", res.stdout)

    def test_watchdog_modes(self):
        cases = [
            ("watchdog: on, 3000 ms", "ON"),
            ("watchdog: not available, no freeze guard", "OFF"),
            ("watchdog: resets at once on RSTC (emulator)", "EMULATOR"),
            ("no mention of watchdog", "NOT REPORTED"),
        ]
        for line, expected in cases:
            files = {
                "bootprev.txt": CLEAN_BOOTPREV,
                "bootlog.txt": f"[0.000] {line}\n",
                "lastrun.txt": "log\n",
            }
            res = self.run_scan(files)
            self.assertIn(f"watchdog {expected}", res.stdout)

    def test_lastrun_log_full(self):
        content = "line 1\nline 2\n--- (the log is full) the last lines printed ---\nline 3\n"
        files = {
            "bootprev.txt": CLEAN_BOOTPREV,
            "bootlog.txt": CLEAN_BOOTLOG,
            "lastrun.txt": content,
        }
        res = self.run_scan(files)
        self.assertIn("lastrun: PRESENT (4 lines, log full)", res.stdout)

    def test_secret_clear_leak_exit_1(self):
        files = {
            "bootprev.txt": CLEAN_BOOTPREV + "connecting with test-pass-4242 here\n",
            "bootlog.txt": CLEAN_BOOTLOG,
            "lastrun.txt": "log\n",
        }
        res = self.run_scan(files, extra_args=["--secret", "test-pass-4242"])
        self.assertEqual(res.returncode, 1)
        self.assertIn("secret #1: LEAK in bootprev.txt (1 times)", res.stdout)
        self.assertNotIn("test-pass-4242", res.stdout)

    def test_secret_masked_exit_0(self):
        files = {
            "bootprev.txt": CLEAN_BOOTPREV + "connecting with ************** here\n",
            "bootlog.txt": CLEAN_BOOTLOG,
            "lastrun.txt": "log\n",
        }
        res = self.run_scan(files, extra_args=["--secret", "test-pass-4242"])
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertIn("secret #1: OK", res.stdout)

    def test_secret_embedded_alphanumeric_not_leak(self):
        files = {
            "bootprev.txt": CLEAN_BOOTPREV + "hash xtest-pass-4242y is fine\n",
            "bootlog.txt": CLEAN_BOOTLOG,
            "lastrun.txt": "log\n",
        }
        res = self.run_scan(files, extra_args=["--secret", "test-pass-4242"])
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertIn("secret #1: OK", res.stdout)

    def test_secret_shorter_than_4_chars_note_exit_0(self):
        files = {
            "bootprev.txt": CLEAN_BOOTPREV,
            "bootlog.txt": CLEAN_BOOTLOG,
            "lastrun.txt": "log\n",
        }
        res = self.run_scan(files, extra_args=["--secret", "abc"])
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertIn("secret #1: shorter than 4 characters, the kernel does not mask it", res.stdout)

    def test_nonexistent_dir_exit_2(self):
        res = subprocess.run([sys.executable, SCRIPT, "/nonexistent/path/xyz"], capture_output=True, text=True)
        self.assertEqual(res.returncode, 2)
        self.assertIn("ERROR", res.stderr)


if __name__ == "__main__":
    unittest.main()

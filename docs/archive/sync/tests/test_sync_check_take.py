"""tools/sync_check.py: an assigned WO that nobody took within TAKE_MIN minutes is flagged (SYNC-EN-7)."""
import datetime
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import sync_check as sc  # noqa: E402

NOW = datetime.datetime.now().astimezone().replace(second=0, microsecond=0)


def ago(minutes):
    return NOW - datetime.timedelta(minutes=minutes)


def assigned(minutes_ago, message):
    t = ago(minutes_ago)
    return t, "| %s | SPV | `b/x` | `ASSIGNED` | %s |" % (t.isoformat(timespec="minutes"), message)


def delivered_text(*items):
    """an operator file with DELIVERED lines: items are (WO, minutes ago)"""
    lines = ["DELIVERED %s @ b@abc1234 · verify: t -> ok · %s" % (wo, ago(m).isoformat(timespec="minutes"))
             for wo, m in items]
    return "---\nschema: 1\nid: NET\n---\n# NET\n\n## Deliveries\n" + "\n".join(lines) + "\n\n## Progress\n"


class T(unittest.TestCase):
    def check(self, state, log, text="## Deliveries\n"):
        return sc.untaken("NET", state, log, text, NOW)

    def test_free_at_11_minutes_is_flagged_with_the_exact_line(self):
        self.assertEqual(self.check("FREE", [assigned(11, "NET: XX-1")]),
                         "NET: XX-1 assigned 11 min ago, not taken; wake the operator or tell the user")

    def test_9_minutes_is_not_flagged(self):
        self.assertIsNone(self.check("FREE", [assigned(9, "NET: XX-1")]))

    def test_working_and_done_are_not_flagged(self):
        for state in ("WORKING", "DONE"):
            self.assertIsNone(self.check(state, [assigned(60, "NET: XX-1")]), state)

    def test_waiting_is_flagged(self):
        self.assertIn("XX-1 assigned 60 min ago", self.check("WAITING", [assigned(60, "NET: XX-1")]))

    def test_spv_is_never_flagged(self):
        self.assertIsNone(sc.untaken("SPV", "FREE", [assigned(60, "SPV: XX-1")], "## Deliveries\n", NOW))

    def test_two_old_queued_wos_give_one_line_naming_the_first(self):
        msg = self.check("FREE", [assigned(30, "NET: XX-1, XX-2")])
        self.assertTrue(msg.startswith("NET: XX-1 assigned 30 min ago"), msg)
        self.assertNotIn("XX-2", msg)

    def test_after_the_first_is_delivered_the_second_clock_starts_at_that_delivery(self):
        log = [assigned(30, "NET: XX-1, XX-2")]
        self.assertIsNone(self.check("FREE", log, delivered_text(("XX-1", 5))))         # head XX-2 for 5 min
        msg = self.check("FREE", log, delivered_text(("XX-1", 12)))                     # head XX-2 for 12 min
        self.assertTrue(msg.startswith("NET: XX-2 assigned 12 min ago"), msg)

    def test_everything_delivered_is_not_flagged(self):
        self.assertIsNone(self.check("FREE", [assigned(60, "NET: XX-1")], delivered_text(("XX-1", 50))))

    def test_legacy_free_text_row_is_ignored(self):
        self.assertIsNone(self.check("FREE", [assigned(60, "NET: D09-1f, D09-1g ... (stacked, take them in order)")]))

    def test_rows_for_other_operators_are_ignored(self):
        self.assertIsNone(self.check("FREE", [assigned(60, "AGY: XX-1")]))

    def test_leading_digit_and_dotted_ids(self):
        # `sync_log.py assign` accepts IDs such as 3D-1 and A.b_2-1 (SYNC-EN-7-fix)
        self.assertEqual(self.check("FREE", [assigned(11, "NET: 3D-1")]),
                         "NET: 3D-1 assigned 11 min ago, not taken; wake the operator or tell the user")
        self.assertIsNone(self.check("FREE", [assigned(9, "NET: 3D-1")]))
        self.assertIn("A.b_2-1 assigned 11 min ago", self.check("FREE", [assigned(11, "NET: A.b_2-1")]))
        wo, _ = sc.queue_head("NET", [assigned(30, "NET: 3D-1, 3D-2")], {"3D-1": ago(5)})
        self.assertEqual(wo, "3D-2")

    def test_queue_head_second_row_starts_after_first_row_delivery(self):
        log = [assigned(60, "NET: XX-1"), assigned(40, "NET: XX-2")]
        wo, start = sc.queue_head("NET", log, {"XX-1": ago(15)})
        self.assertEqual(wo, "XX-2")
        self.assertEqual(start, ago(15))


if __name__ == "__main__":
    unittest.main()

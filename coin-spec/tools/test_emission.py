#!/usr/bin/env python3
"""JSON-only validation for coin-spec/emission_schedule.json (no Boost)."""
from __future__ import annotations

import json
import os
import unittest

COIN_UNITS = 100_000_000
POS_CAP_UNITS = 200_000 * COIN_UNITS
ERA_BLOCKS = 500_000
FIRST_SUBSIDY_HEIGHT = 1
DECAY_NUM = 90
DECAY_DEN = 100
R0_UNITS = 4_000_000

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
JSON_PATH = os.path.join(ROOT, "coin-spec", "emission_schedule.json")


class TestEmissionSchedule(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(JSON_PATH, encoding="utf-8") as f:
            cls.doc = json.load(f)
        cls.eras = cls.doc["eras"]
        cls.params = cls.doc["params"]

    def test_params(self):
        self.assertEqual(self.params["pos_subsidy_budget_units"], POS_CAP_UNITS)
        self.assertEqual(self.params["first_subsidy_height"], FIRST_SUBSIDY_HEIGHT)
        self.assertEqual(self.params["era_blocks"], ERA_BLOCKS)
        self.assertEqual(self.params["r0_units_per_block"], R0_UNITS)
        self.assertTrue(self.params["no_tail_emission"])
        self.assertTrue(self.params["final_clamp"])

    def test_total_equals_cap(self):
        total = sum(e["max_era_subsidy_units"] for e in self.eras)
        self.assertEqual(total, POS_CAP_UNITS)
        self.assertEqual(self.doc["summary"]["lifetime_pos_subsidy_units"], POS_CAP_UNITS)

    def test_contiguous_heights(self):
        self.assertEqual(self.eras[0]["era_start_height"], FIRST_SUBSIDY_HEIGHT)
        for i in range(1, len(self.eras)):
            self.assertEqual(
                self.eras[i]["era_start_height"],
                self.eras[i - 1]["era_end_height"] + 1,
            )

    def test_first_era_reward(self):
        self.assertEqual(self.eras[0]["reward_units_per_block"], R0_UNITS)
        self.assertEqual(self.eras[0]["era_block_count"], ERA_BLOCKS)

    def test_no_tail_after_last(self):
        last = self.eras[-1]
        self.assertGreater(last["reward_units_per_block"], 0)
        # heights beyond last era have zero subsidy in core (GetProofOfStakeSubsidy)

    def test_decay_between_full_eras(self):
        for i in range(1, len(self.eras)):
            prev = self.eras[i - 1]["reward_units_per_block"]
            cur = self.eras[i]["reward_units_per_block"]
            if self.eras[i]["era_block_count"] == ERA_BLOCKS and self.eras[i - 1]["era_block_count"] == ERA_BLOCKS:
                if prev > 1:
                    self.assertIn(cur, (prev * DECAY_NUM // DECAY_DEN, (prev * DECAY_NUM // DECAY_DEN) + 1))


if __name__ == "__main__":
    unittest.main()

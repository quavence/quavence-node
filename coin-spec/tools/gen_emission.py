#!/usr/bin/env python3
"""
Integer-only PoS emission schedule for Quavence.

All amounts in smallest units (1 COIN = 100_000_000 units). No float arithmetic.

Outputs:
  coin-spec/emission_schedule.json
  coin-spec/emission_schedule.csv
  src/consensus/subsidy_schedule.h  (with --header)

Default: export from src/consensus/subsidy_schedule.h (canonical with built tree).
Regenerate from spec: --compute (integer simulation; must match cap exactly).

  python3 coin-spec/tools/gen_emission.py
  python3 coin-spec/tools/gen_emission.py --compute --header
  python3 -m unittest coin-spec/tools/test_emission.py -v
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
from typing import List, Tuple

COIN_UNITS = 100_000_000
HARD_CAP_COIN = 250_000
GENESIS_ALLOCATION_COIN = 50_000
POS_BUDGET_COIN = 200_000
POS_CAP_UNITS = POS_BUDGET_COIN * COIN_UNITS
ERA_BLOCKS = 500_000
FIRST_SUBSIDY_HEIGHT = 1
DECAY_NUM = 90
DECAY_DEN = 100
R0_UNITS = 4_000_000  # 0.04 COIN per block (era 0)


def format_coin(units: int) -> str:
    return f"{units // COIN_UNITS}.{units % COIN_UNITS:08d}"


def mk_era(
    era_index: int,
    era_start: int,
    era_end: int,
    reward: int,
) -> dict:
    block_count = era_end - era_start + 1
    era_subsidy = block_count * reward
    return {
        "era_index": era_index,
        "era_start_height": era_start,
        "era_end_height": era_end,
        "era_block_count": block_count,
        "reward_units_per_block": reward,
        "reward_coin_per_block": format_coin(reward),
        "max_era_subsidy_units": era_subsidy,
        "max_era_subsidy_coin": format_coin(era_subsidy),
    }


def compute_schedule() -> List[dict]:
    """Integer decay * 90/100 per era; final era pays exactly remaining budget."""
    eras: List[dict] = []
    remaining = POS_CAP_UNITS
    height = FIRST_SUBSIDY_HEIGHT
    reward = R0_UNITS
    era_index = 0

    while remaining > 0:
        full = ERA_BLOCKS * reward
        if full <= remaining:
            end = height + ERA_BLOCKS - 1
            eras.append(mk_era(era_index, height, end, reward))
            remaining -= full
            height = end + 1
            era_index += 1
            reward = reward * DECAY_NUM // DECAY_DEN
            if reward == 0:
                reward = 1
            continue

        # Final clamp: exhaust remaining exactly; no tail emission after this era.
        blocks = ERA_BLOCKS
        if remaining % blocks == 0:
            reward = remaining // blocks
        else:
            # Partial last era at decayed reward, then optional bump (see existing header).
            reward = max(1, reward)
            blocks = remaining // reward
            if blocks == 0:
                blocks = 1
            paid = blocks * reward
            if paid < remaining:
                blocks = ERA_BLOCKS
                reward = (remaining + blocks - 1) // blocks
        end = height + blocks - 1
        paid = blocks * reward
        if paid != remaining:
            raise SystemExit(
                f"final era mismatch: paid {paid} != remaining {remaining} "
                f"(reward={reward}, blocks={blocks})"
            )
        eras.append(mk_era(era_index, height, end, reward))
        remaining = 0

    total = sum(e["max_era_subsidy_units"] for e in eras)
    if total != POS_CAP_UNITS:
        raise SystemExit(f"total {total} != cap {POS_CAP_UNITS}")
    return eras


def parse_subsidy_header(path: str) -> List[dict]:
    """Read SUBSIDY_ERAS rows from existing C++ header."""
    rows = []
    pat = re.compile(
        r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*int64_t\((\d+)\)\s*\}"
    )
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = pat.search(line)
            if m:
                start, end, reward = int(m.group(1)), int(m.group(2)), int(m.group(3))
                rows.append(mk_era(len(rows), start, end, reward))
    if not rows:
        raise SystemExit(f"no eras parsed from {path}")
    return rows


def write_json(path: str, eras: List[dict]) -> None:
    total = sum(e["max_era_subsidy_units"] for e in eras)
    doc = {
        "params": {
            "hard_cap_coin": HARD_CAP_COIN,
            "genesis_allocation_coin": GENESIS_ALLOCATION_COIN,
            "pos_subsidy_budget_coin": POS_BUDGET_COIN,
            "hard_cap_units": HARD_CAP_COIN * COIN_UNITS,
            "genesis_allocation_units": GENESIS_ALLOCATION_COIN * COIN_UNITS,
            "pos_subsidy_budget_units": POS_CAP_UNITS,
            "era_blocks": ERA_BLOCKS,
            "decay_ratio": f"{DECAY_NUM}/{DECAY_DEN}",
            "r0_units_per_block": R0_UNITS,
            "r0_coin_per_block": format_coin(R0_UNITS),
            "first_subsidy_height": FIRST_SUBSIDY_HEIGHT,
            "coin_units_per_coin": COIN_UNITS,
            "no_tail_emission": True,
            "final_clamp": True,
        },
        "summary": {
            "era_count": len(eras),
            "lifetime_pos_subsidy_units": total,
            "lifetime_pos_subsidy_coin": format_coin(total),
            "last_subsidy_height": eras[-1]["era_end_height"],
        },
        "eras": eras,
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")


def write_csv(path: str, eras: List[dict]) -> None:
    fields = [
        "era_index", "era_start_height", "era_end_height", "era_block_count",
        "reward_units_per_block", "reward_coin_per_block",
        "max_era_subsidy_units", "max_era_subsidy_coin",
    ]
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for row in eras:
            w.writerow({k: row[k] for k in fields})


def write_header(path: str, eras: List[dict]) -> None:
    lines = [
        "// Auto-generated by coin-spec/tools/gen_emission.py — do not edit by hand.",
        "#ifndef BITCOIN_CONSENSUS_SUBSIDY_SCHEDULE_H",
        "#define BITCOIN_CONSENSUS_SUBSIDY_SCHEDULE_H",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        f"static const int SUBSIDY_FIRST_HEIGHT = {FIRST_SUBSIDY_HEIGHT};",
        f"static const int SUBSIDY_ERA_BLOCKS = {ERA_BLOCKS};",
        f"static const int64_t SUBSIDY_POS_CAP_UNITS = int64_t({POS_BUDGET_COIN}) * int64_t({COIN_UNITS});",
        "",
        "struct SubsidyEraRow {",
        "    int eraStartHeight;",
        "    int eraEndHeight;",
        "    int64_t rewardPerBlock;",
        "};",
        "",
        "static const SubsidyEraRow SUBSIDY_ERAS[] = {",
    ]
    for e in eras:
        lines.append(
            f"    {{ {e['era_start_height']}, {e['era_end_height']}, "
            f"int64_t({e['reward_units_per_block']}) }},"
        )
    lines.extend([
        "};",
        f"static const size_t SUBSIDY_ERA_COUNT = {len(eras)};",
        "",
        "#endif // BITCOIN_CONSENSUS_SUBSIDY_SCHEDULE_H",
        "",
    ])
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--compute",
        action="store_true",
        help="Recompute schedule from spec (default: export from src/consensus/subsidy_schedule.h)",
    )
    parser.add_argument("--header", action="store_true", help="Also write src/consensus/subsidy_schedule.h")
    args = parser.parse_args()

    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    coin_spec = os.path.join(root, "coin-spec")
    hdr_path = os.path.join(root, "src", "consensus", "subsidy_schedule.h")

    if args.compute:
        eras = compute_schedule()
        source = "computed"
    else:
        eras = parse_subsidy_header(hdr_path)
        source = hdr_path

    total = sum(e["max_era_subsidy_units"] for e in eras)
    if total != POS_CAP_UNITS:
        raise SystemExit(f"total {total} != cap {POS_CAP_UNITS} (source={source})")

    write_json(os.path.join(coin_spec, "emission_schedule.json"), eras)
    write_csv(os.path.join(coin_spec, "emission_schedule.csv"), eras)
    print(f"Exported {len(eras)} eras, total {total} units from {source}")

    if args.header:
        write_header(hdr_path, eras)
        print(f"Wrote {hdr_path}")


if __name__ == "__main__":
    main()

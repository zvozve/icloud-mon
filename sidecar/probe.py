#!/usr/bin/env python3
# ======================================================================
#  probe.py  -  一次性采样, 打印 JSON。用于验证旁路读到的数值与
#               C++ 版一致 (对照 build/Release 版本界面或运行日志)。
#
#  用法:
#    python sidecar/probe.py            # 一次采样, 打印关键字段
#    python sidecar/probe.py --full     # 打印完整 payload
#    python sidecar/probe.py --watch 5  # 每 5 秒连续采样
# ======================================================================
from __future__ import annotations

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icloudmon.bridge import build                     # noqa: E402
from icloudmon.procs import running_processes          # noqa: E402
from icloudmon.snapshot import sample                  # noqa: E402
from icloudmon.stall import detect_stall               # noqa: E402

KEY_FIELDS = [
    "stateName", "stateDetail", "busy", "inSync", "dbOk", "clientVersion",
    "upDone", "upTotal", "upCount", "upSize", "upBusy",
    "dlDone", "dlTotal", "dlCount", "dlSize", "dlPending", "dlBusy",
    "delDone", "delTotal", "delCount", "delBusy",
    "faults", "overQuota", "rankText",
    "cloudOnlyCount", "pendingRealCount", "pendingOrphanCount",
    "localPendingTotal", "icloudRunning", "icloudProcesses",
    "pcsBuckets", "anomalous", "anomalyList", "elapsedMs",
]


def once(full: bool) -> dict:
    s = sample()
    st = detect_stall(300)
    return build(s, st, running=running_processes())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--full", action="store_true", help="打印完整 payload")
    ap.add_argument("--watch", type=int, metavar="SEC", help="连续采样, 间隔秒")
    args = ap.parse_args()

    while True:
        try:
            payload = once(args.full)
        except Exception as e:                          # noqa: BLE001
            print(f"[ERROR] 采样失败: {e}")
            return 2

        if args.full:
            print(json.dumps(payload, ensure_ascii=False, indent=2))
        else:
            out = {k: payload[k] for k in KEY_FIELDS if k in payload}
            print(json.dumps(out, ensure_ascii=False, indent=2))

        if not args.watch:
            return 0
        time.sleep(args.watch)


if __name__ == "__main__":
    raise SystemExit(main())

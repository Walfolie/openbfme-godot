#!/usr/bin/env python3
"""Fork lane FULLCHECK-1 (plan phase 2): every check of the fork in one run on the owner's machine, one summary at the end.

GitHub's runners have no game files and tools/release/public_workflows.py keeps the repository's workflows off the owner's PC, so the checks that
need RotWK run here, started by hand (before a release or a big upstream merge), in this order (a step that fails is reported and the rest still run):
  1. upstream:  tools/fork/upstream_sync.py (what upstream has that the fork lacks; a trial merge, nothing changed);
  2. build:     build.bat (Windows) or cmake --build engine/build (elsewhere), with --jobs build jobs (the night run shares the machine);
  3. tests:     tools/fork/retail_check.py --jobs N (every C++ test with the game data, sim_audit, the Godot tests);
  4. soak:      tools/net/soak.py --hours H (impaired network games: desyncs, crashes, stalls).
Everything goes to workspace/fork/full_check/<date>/ (ignored): every step's log, summary.txt and summary.json.

    python tools/fork/full_check.py [--soak-hours 1] [--jobs 3] [--skip build,tests,...]
Run it from the MSVC environment (vcvars64) with ROTWK_INSTALL, BFME2_INSTALL and GODOT set, as run_tests.bat needs. Exit 0 when every step passed.
It takes a few hours with the soak; --skip soak (or a short --soak-hours) for a quicker check.
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STEPS = ("upstream", "build", "tests", "soak")


def run(name: str, cmd: list[str], out: Path, env: dict) -> dict:
    log = out / f"{name}.log"
    t0 = time.monotonic()
    print(f"== {name}: {' '.join(cmd)}", flush=True)
    with open(log, "w", encoding="utf-8", errors="replace") as f:
        rc = subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT).returncode
    tail = log.read_text(encoding="utf-8", errors="replace").splitlines()[-12:]
    print(f"   exit {rc} in {int(time.monotonic() - t0)} s", flush=True)
    return {"step": name, "exit": rc, "seconds": int(time.monotonic() - t0), "log": str(log), "tail": tail}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--soak-hours", type=float, default=1.0)
    ap.add_argument("--jobs", type=int, default=3, help="unit test processes and build jobs (memory: each test process loads the game data)")
    ap.add_argument("--skip", default="", help=f"steps to leave out ({', '.join(STEPS)})")
    a = ap.parse_args()
    skip = {s for s in a.skip.split(",") if s}
    if skip - set(STEPS):
        print(f"ERROR: unknown step(s) {', '.join(sorted(skip - set(STEPS)))}", file=sys.stderr)
        return 1
    out = ROOT / "workspace" / "fork" / "full_check" / datetime.datetime.now().strftime("%Y%m%d-%H%M")
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    py = sys.executable
    results = []
    if "upstream" not in skip:
        results.append(run("upstream", [py, "tools/fork/upstream_sync.py"], out, env))
    if "build" not in skip:
        if os.name == "nt":
            env_build = dict(env, CMAKE_BUILD_PARALLEL_LEVEL=str(a.jobs))
            results.append(run("build", ["cmd", "/c", str(ROOT / "build.bat")], out, env_build))
        else:
            results.append(run("build", ["cmake", "--build", "engine/build", "-j", str(a.jobs)], out, env))
    if "tests" not in skip:
        results.append(run("tests", [py, "tools/fork/retail_check.py", "--jobs", str(a.jobs), "--out", str(out / "retail")], out, env))
    if "soak" not in skip:
        results.append(run("soak", [py, "tools/net/soak.py", "--hours", str(a.soak_hours), "--out", str(out / "soak")], out, env))
    # upstream_sync: 0 up to date or clean, 2 a merge would conflict (worth knowing, not a failure of the fork)
    failed = [r for r in results if r["exit"] != 0 and not (r["step"] == "upstream" and r["exit"] == 2)]
    lines = [f"full check {out.name}: {'PASSED' if not failed else 'FAILED (' + ', '.join(r['step'] for r in failed) + ')'}"]
    for r in results:
        lines.append(f"{r['step']}: exit {r['exit']} in {r['seconds'] // 60} min")
        lines += [f"    {t}" for t in r["tail"][-4:]]
    (out / "summary.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    (out / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())

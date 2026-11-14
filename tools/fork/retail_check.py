#!/usr/bin/env python3
"""Fork lane MP-0: the retail check, the fork's CI run against the owner's own RotWK 2.01 / BFME2 1.06 install, on the owner's own machine.

The public pull request checks (.github/workflows/pr.yml) have no game files, so every retail test SKIPs there. A self-hosted GitHub runner would
run them, but tools/release/public_workflows.py refuses one in .github/workflows (a pull request from anyone would run code on the owner's PC),
so this check runs locally instead, on the tree checked out here. What it runs, without stopping at the first failure (run_tests.bat does):
  1. the C++ unit tests (engine/build/openbfme_tests.exe, run from engine/build as run_tests.bat does). With --jobs N the test files are split over
     N processes run at once: the networking files (fixed LAN ports 8086 .. 8093, timing, desync dumps in the working directory) stay together in
     one process, the others are dealt out in turn, and each process runs in its own working directory (engine/build-shard-<n>, ignored) so
     files written under relative names never collide. Every process loads the game data itself: mind the memory (a full run was once stopped
     for want of it);
  2. the simulation floating-point audit (tools/sim/sim_audit.py);
  3. Godot: --import, the headless smoke test, then the windowed shader, tangent convention and road shader tests (unless --no-godot).
RW_GAME_DAT defaults to <ROTWK_INSTALL>/game.dat, so the binary-fact tests run too.
It fails (exit 1) on a failed test case, a failed step, or a SKIP because game data was missing (a retail test that did not run is not a pass).
SKIPs the install cannot change are listed, not failed: a test pinned to English data on another language's install, a file system without
symbolic links, an optional tool input (OPENBFME_ASSET_PACK). The summary (summary.json, summary.txt) and every step's log go to --out
(default workspace/fork/retail/<time>, an ignored folder: logs may quote game text, and nothing derived from game files is committed).

    python tools/fork/retail_check.py [--tests "<doctest --source-file filter>"] [--jobs N] [--no-godot] [--out DIR]
Needs ROTWK_INSTALL, BFME2_INSTALL and (unless --no-godot) GODOT; run it from the MSVC environment (vcvars64) as run_tests.bat is.
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REQUIRED = ("ROTWK_INSTALL", "BFME2_INSTALL", "RW_GAME_DAT")
GODOT_TESTS = ("shader_test", "tangent_convention_test", "road_shader_test")

SUMMARY = re.compile(r"^\[doctest\] test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed\s*\|\s*(\d+) skipped", re.M)
FAILURE = re.compile(r"^(.+?\.(?:cpp|h))\((\d+)\): (?:ERROR|FATAL ERROR): (.*)$", re.M)
SKIP = re.compile(r"^.*?\bSKIP: (.*)$", re.M)
NAMED_UNSET = re.compile(r"\((?:set )?([A-Z][A-Z0-9_]+)(?: unset)?\)")
# the test files that open sockets or run peers: one process, in file order (the LAN ports are fixed; the peer tests are timing-sensitive)
NETWORK_FILE = re.compile(r"^test_(lan|mp\d|mpdrop|net|online|p16|relay|rejoin|spectate|smooth_net|uberjoin|chat|sdk2_net|wotr\d+_(net|online)|cross)")


def shards(files: list[str], jobs: int) -> list[list[str]]:
    """The test files split for `jobs` processes: shard 0 holds every networking file (and its share of the rest), the others the rest in turn."""
    if jobs <= 1:
        return [files]
    out: list[list[str]] = [[] for _ in range(jobs)]
    rest = []
    for f in files:
        (out[0] if NETWORK_FILE.match(f) else rest).append(f)
    # the networking shard is the slow one: it takes a smaller share of the rest
    order = list(range(1, jobs)) * 2 + [0]
    for i, f in enumerate(rest):
        out[order[i % len(order)]].append(f)
    return [s for s in out if s]


def classify_skip(text: str, unset: set[str]) -> str:
    """'data' (game data the run should have had), 'language' (pinned to English data) or 'other'."""
    if "pinned to the English retail data" in text:
        return "language"
    named = set(NAMED_UNSET.findall(text))
    if "RW_GAME_DAT" in text and "RW_GAME_DAT" in unset:
        return "data"
    if named & set(REQUIRED):
        return "data"
    if named:
        return "other"  # an optional input the test names itself (OPENBFME_ASSET_PACK ...)
    if re.search(r"\b(ROTWK_INSTALL|BFME2_INSTALL)\b", text) and unset & {"ROTWK_INSTALL", "BFME2_INSTALL"}:
        return "data"
    return "other"


def parse_unit_log(text: str, unset: set[str]) -> dict:
    m = SUMMARY.findall(text)
    cases = [int(x) for x in m[-1]] if m else None
    skips: dict[str, list[str]] = {"data": [], "language": [], "other": []}
    for s in SKIP.findall(text):
        s = s.strip()
        kind = classify_skip(s, unset)
        if s not in skips[kind]:
            skips[kind].append(s)
    failures = sorted({f"{Path(f).name}:{line}: {what.strip()}" for f, line, what in FAILURE.findall(text)})
    return {"cases": dict(zip(("total", "passed", "failed", "skipped"), cases)) if cases else None, "failures": failures, "skips": skips}


def merge_units(parts: list[dict]) -> dict:
    """One unit result from the shards' (the case counts summed; a shard without a summary leaves the whole without one)."""
    if len(parts) == 1:
        return parts[0]
    cases = None
    if all(p["cases"] for p in parts):
        cases = {k: sum(p["cases"][k] for p in parts) for k in ("total", "passed", "failed")}
        cases["skipped"] = parts[0]["cases"]["total"] + parts[0]["cases"]["skipped"] - cases["total"]  # every shard sees every case
    skips: dict[str, list[str]] = {"data": [], "language": [], "other": []}
    for p in parts:
        for kind, lines in p["skips"].items():
            skips[kind] += [s for s in lines if s not in skips[kind]]
    return {"cases": cases, "failures": sorted({f for p in parts for f in p["failures"]}), "skips": skips}


def run(step: str, cmd: list[str], cwd: Path, env: dict, out: Path) -> int:
    log = out / f"{step}.log"
    print(f"== {step}: {' '.join(cmd)}", flush=True)
    with open(log, "w", encoding="utf-8", errors="replace") as f:
        code = subprocess.run(cmd, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT).returncode
    print(f"   exit {code} ({log})", flush=True)
    return code


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--tests", default="", help="a doctest --source-file filter (e.g. '*test_fb7*'); default every test")
    ap.add_argument("--jobs", type=int, default=1, help="unit test processes run at once (networking files stay in one)")
    ap.add_argument("--no-godot", action="store_true", help="skip the Godot smoke and shader tests")
    ap.add_argument("--out", type=Path, default=None)
    a = ap.parse_args()
    env = dict(os.environ)
    if env.get("ROTWK_INSTALL") and not env.get("RW_GAME_DAT") and (Path(env["ROTWK_INSTALL"]) / "game.dat").is_file():
        env["RW_GAME_DAT"] = str(Path(env["ROTWK_INSTALL"]) / "game.dat")
    unset = {v for v in REQUIRED if not env.get(v)}
    out = a.out or ROOT / "workspace" / "fork" / "retail" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)
    problems = [f"{v} is not set" for v in sorted(unset)]
    steps: dict[str, int] = {}

    tests = ROOT / "engine" / "build" / ("openbfme_tests.exe" if os.name == "nt" else "openbfme_tests")
    if not tests.is_file():
        print(f"ERROR: {tests} not found; run build.bat first", file=sys.stderr)
        return 1
    if a.jobs <= 1:
        steps["unit"] = run("unit", [str(tests)] + ([f"--source-file={a.tests}"] if a.tests else []), tests.parent, env, out)
        logs = [out / "unit.log"]
    else:
        if a.tests:
            print("ERROR: --tests and --jobs together are not supported (the shards are whole files)", file=sys.stderr)
            return 1
        files = sorted(p.name for p in (ROOT / "engine" / "tests").glob("test_*.cpp"))
        procs = []
        for n, group in enumerate(shards(files, a.jobs)):
            cwd = ROOT / "engine" / f"build-shard-{n}"
            cwd.mkdir(exist_ok=True)
            log = out / f"unit-{n}.log"
            cmd = [str(tests), "--source-file=" + ",".join("*" + f for f in group)]
            print(f"== unit shard {n}: {len(group)} files ({log})", flush=True)
            f = open(log, "w", encoding="utf-8", errors="replace")
            procs.append((n, subprocess.Popen(cmd, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT), f))
        for n, proc, f in procs:
            code = proc.wait()
            f.close()
            print(f"   shard {n} exit {code}", flush=True)
            steps[f"unit-{n}"] = code
        logs = [out / f"unit-{n}.log" for n, _, _ in procs]
    unit = merge_units([parse_unit_log(p.read_text(encoding="utf-8", errors="replace"), unset) for p in logs])
    if unit["cases"] is None:
        problems.append("the unit tests printed no doctest summary (a crash?)")
    steps["sim_audit"] = run("sim_audit", [sys.executable, str(ROOT / "tools" / "sim" / "sim_audit.py"), "--build", str(ROOT / "engine" / "build")],
                             ROOT, env, out)
    if not a.no_godot:
        godot = env.get("GODOT", "")
        if not godot or not Path(godot).is_file():
            problems.append("GODOT is not the Godot 4.7 console executable")
        else:
            project = str(ROOT / "godot")
            run("godot_import", [godot, "--headless", "--path", project, "--import"], ROOT, env, out)
            steps["smoke"] = run("smoke", [godot, "--headless", "--path", project, "--script", "res://tests/smoke_test.gd"], ROOT, env, out)
            for t in GODOT_TESTS:
                steps[t] = run(t, [godot, "--path", project, "--script", f"res://tests/{t}.gd"], ROOT, env, out)

    for step, code in steps.items():
        if code != 0:
            problems.append(f"{step} exited {code}")
    if unit["skips"]["data"]:
        problems.append(f"{len(unit['skips']['data'])} test(s) skipped for missing game data")
    summary = {"tree": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip(),
               "dirty": bool(subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, capture_output=True, text=True).stdout.strip()),
               "filter": a.tests, "steps": steps, "unit": unit, "problems": problems, "passed": not problems}
    (out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    lines = [f"retail check {'PASSED' if not problems else 'FAILED'} (tree {summary['tree'][:12]}{', dirty' if summary['dirty'] else ''})"]
    if unit["cases"]:
        c = unit["cases"]
        lines.append(f"unit tests: {c['total']} cases, {c['passed']} passed, {c['failed']} failed, {c['skipped']} skipped (filtered out or doctest::skip)")
    lines += [f"step {s}: exit {c}" for s, c in steps.items()]
    lines += [f"FAIL {f}" for f in unit["failures"]]
    for kind in ("data", "language", "other"):
        lines += [f"SKIP ({kind}) {s}" for s in unit["skips"][kind]]
    lines += [f"PROBLEM {p}" for p in problems]
    (out / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())

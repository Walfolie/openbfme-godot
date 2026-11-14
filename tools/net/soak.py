#!/usr/bin/env python3
"""Fork lane SOAK-1 (plan phase 2): many automated network games on this machine, on impaired links, counting desyncs, crashes and stalls.

Every game is a LAN game of openbfme_peer processes on 127.0.0.1: one per human slot (2 to 4; the skirmish AI fills the other slots, so a 4v4 is
four peers and four computer players and the memory stays moderate), each a scripted player (GameNetwork/ScriptedPlayer.h). Every peer's outgoing
datagrams go through the transport's fault injector (--link, GameNetwork/NetImpairment.h) with the run's link profile. The script rotates through
the game table (1v1 to 4v4, every faction, several maps) and the link profiles and judges each game:
  ok       every peer finished every frame (a peer planted to vanish: up to its stop), no desync, every frame's hash equal on all peers;
  desync   a peer reported a desync, or two peers' hashes of one frame differ (tools/replay/desync_bisect.py --dump runs on the first dump);
  crash    a peer exited abnormally or wrote no report;
  network  a peer gave up on the network (exit 3: a stall or a timeout) or on its recording (exit 4).
Every game's folder keeps the reports, hashes, the host's replay and any desync dumps; results.jsonl gets one line per game and summary.txt the
totals. --hours keeps playing until the time is up (for a night run), --games plays that many.

    python tools/net/soak.py [--peer EXE] [--games N | --hours H] [--frames N] [--profiles lan,dsl,...] [--out DIR] [--port N]
Needs ROTWK_INSTALL / BFME2_INSTALL. Exit 0: every game ok; 2: a desync or a crash; 1: a usage error; 3: only network failures.
"""
from __future__ import annotations

import argparse
import datetime
import itertools
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PEER = ROOT / "engine" / "build" / ("openbfme_peer.exe" if os.name == "nt" else "openbfme_peer")
F4 = "maps/map mp fall back 4p/map mp fall back 4p.map"
F8 = "maps/map mp fall back 8p/map mp fall back 8p.map"

# the games: a name, a map, the slots in order (the "human" slots are the peers: the first hosts, the others join in order)
GAMES = [
    ("1v1", F4, ["human,FactionMen,0,0", "human,FactionMordor,1,1"]),
    ("2v2", F4, ["human,FactionElves,0,0", "human,FactionIsengard,1,1", "human,FactionDwarves,2,0", "human,FactionAngmar,3,1"]),
    ("1v1", F4, ["human,FactionWild,0,0", "human,FactionDwarves,1,1"]),
    ("3v3", F8, ["human,FactionAngmar,0,0", "human,FactionMen,1,1", "human,FactionMordor,2,0", "hard,FactionElves,3,1", "hard,FactionIsengard,4,0",
                 "medium,FactionWild,5,1"]),
    ("4v4", F8, ["human,FactionMen,0,0", "human,FactionElves,1,0", "human,FactionIsengard,2,1", "human,FactionDwarves,3,1", "hard,FactionMordor,4,1",
                 "brutal,FactionWild,5,0", "medium,FactionAngmar,6,0", "hard,FactionMen,7,1"]),
    ("2v2", F4, ["human,FactionMordor,0,0", "human,FactionAngmar,1,1", "hard,FactionWild,2,0", "medium,FactionMen,3,1"]),
    ("4v4", F8, ["human,FactionWild,0,0", "human,FactionAngmar,1,1", "human,FactionMordor,2,0", "human,FactionMen,3,1", "hard,FactionElves,4,1",
                 "hard,FactionDwarves,5,0", "hard,FactionIsengard,6,0", "brutal,FactionAngmar,7,1"]),
]

# the link profiles: every peer's --link faults, and the extra arguments of the peer they apply to ("last": the last joiner only)
PROFILES = {
    "lan": {"link": "", "last": []},
    "dsl": {"link": "latency=40,jitter=10,loss=5", "last": []},
    "wan": {"link": "latency=150,jitter=50,loss=20,dup=10,reorder=10", "last": []},
    "bad": {"link": "latency=300,jitter=100,loss=50,dup=10,reorder=20", "last": []},
    "blackout": {"link": "latency=60,jitter=20,loss=10", "last": ["--blackout-at", "{half}:10000"]},
    "vanish": {"link": "latency=60,jitter=20,loss=10", "last": ["--vanish-at", "{half}"]},
}


def report_fields(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in text.splitlines():
        key, _, value = line.partition(" ")
        out.setdefault(key, value)
    return out


def read_hashes(text: str) -> dict[int, str]:
    out = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].isdigit():
            out[int(parts[0])] = parts[1]
    return out


def judge(peers: list[dict], frames: int) -> tuple[str, str]:
    """(outcome, why) of one game from its peers' results: {"rc", "report" (fields or None), "hashes" ({frame: hash}), "planted_stop" (bool)}."""
    for i, p in enumerate(peers):
        if p["report"] is None:
            return "crash", f"peer {i} wrote no report (exit {p['rc']})"
        if p["rc"] not in (0, 2, 3, 4):
            return "crash", f"peer {i} exited {p['rc']}"
    for i, p in enumerate(peers):
        if p["rc"] == 2 or p["report"].get("desyncs", "0") not in ("0", ""):
            return "desync", f"peer {i} reported {p['report'].get('desyncs', '?')} desync(s)"
    frame_sets = [p["hashes"] for p in peers]
    for f in sorted(set().union(*frame_sets)):
        seen = {h[f] for h in frame_sets if f in h}
        if len(seen) > 1:
            return "desync", f"frame {f}: the peers' hashes differ ({', '.join(sorted(seen))})"
    for i, p in enumerate(peers):
        if p["rc"] in (3, 4):
            return "network", f"peer {i} exited {p['rc']}"
        if not p["planted_stop"] and p["report"].get("frames") != str(frames):
            return "network", f"peer {i} ran {p['report'].get('frames', '?')} of {frames} frames"
    return "ok", ""


def play(exe: Path, game: tuple, profile: str, frames: int, seed: int, port: int, folder: Path, timeout_s: int) -> dict:
    name, map_key, slots = game
    humans = sum(1 for s in slots if s.startswith("human,"))
    prof = PROFILES[profile]
    folder.mkdir(parents=True, exist_ok=True)
    common = [str(exe), "--frames", str(frames), "--map", map_key, "--seed", str(seed), "--script", "--run-ahead", "2"]
    if prof["link"]:
        common += ["--link", prof["link"]]
    procs = []
    t0 = time.monotonic()
    for i in range(humans):
        role = ["--host", str(port)] + [x for s in slots for x in ("--slot", s)] + ["--record", str(folder / "host.replay")] if i == 0 else \
               ["--join", f"127.0.0.1:{port}", "--name", f"Joiner{i}"]
        last = [a.format(half=frames // 2) for a in prof["last"]] if i == humans - 1 and i > 0 else []
        cmd = common + role + last + ["--report", str(folder / f"peer{i}.txt"), "--hashes", str(folder / f"peer{i}.hashes")]
        log = open(folder / f"peer{i}.log", "w", encoding="utf-8", errors="replace")
        procs.append((subprocess.Popen(cmd, cwd=folder, stdout=log, stderr=subprocess.STDOUT), log, bool(last) and "--vanish-at" in last))
        if i == 0:
            time.sleep(1.0)  # the host's lobby is up before the joiners
    peers = []
    for proc, log, planted in procs:
        try:
            rc = proc.wait(timeout=max(1, timeout_s - (time.monotonic() - t0)))
        except subprocess.TimeoutExpired:
            proc.kill()
            rc = -9
        log.close()
        peers.append({"rc": rc, "planted_stop": planted})
    for i, p in enumerate(peers):
        rep, hashes = folder / f"peer{i}.txt", folder / f"peer{i}.hashes"
        p["report"] = report_fields(rep.read_text(encoding="utf-8", errors="replace")) if rep.is_file() else None
        p["hashes"] = read_hashes(hashes.read_text(encoding="utf-8", errors="replace")) if hashes.is_file() else {}
    outcome, why = judge(peers, frames)
    result = {"game": name, "players": humans, "slots": len(slots), "map": map_key, "profile": profile, "seed": seed, "frames": frames,
              "outcome": outcome, "why": why, "seconds": round(time.monotonic() - t0, 1), "folder": str(folder),
              "exits": [p["rc"] for p in peers], "stalled_frames": [p["report"].get("stalled_frames") if p["report"] else None for p in peers]}
    if outcome == "desync":
        dumps = sorted(folder.glob("DESYNC-*.txt"))
        if dumps:
            b = subprocess.run([sys.executable, str(ROOT / "tools" / "replay" / "desync_bisect.py"), "--dump", str(dumps[0])], capture_output=True, text=True)
            (folder / "bisect.txt").write_text(b.stdout + b.stderr, encoding="utf-8")
            result["bisect"] = b.stdout.strip().splitlines()[:8]
    return result


def summary(results: list[dict]) -> list[str]:
    by = {}
    for r in results:
        by.setdefault(r["outcome"], 0)
        by[r["outcome"]] += 1
    sizes = {}
    for r in results:
        sizes.setdefault(r["game"], 0)
        sizes[r["game"]] += 1
    lines = [f"{len(results)} games: " + ", ".join(f"{k} {v}" for k, v in sorted(by.items())),
             "by size: " + ", ".join(f"{k} {v}" for k, v in sorted(sizes.items())),
             "frames played: " + str(sum(r["frames"] for r in results))]
    for r in results:
        if r["outcome"] != "ok":
            lines.append(f"  {r['outcome']}: {r['game']} {r['profile']} seed {r['seed']}: {r['why']} ({r['folder']})")
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--peer", type=Path, default=DEFAULT_PEER)
    ap.add_argument("--games", type=int, default=None, help="play this many games (default: one round of the table, or --hours)")
    ap.add_argument("--hours", type=float, default=None, help="keep playing until this much time has passed")
    ap.add_argument("--frames", type=int, default=3000, help="logic frames per game (3000: ten minutes of game time)")
    ap.add_argument("--profiles", default=",".join(PROFILES), help=f"the link profiles to rotate through ({', '.join(PROFILES)})")
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--port", type=int, default=29000)
    ap.add_argument("--seed", type=int, default=7000)
    a = ap.parse_args()
    if not a.peer.is_file():
        print(f"ERROR: {a.peer} not found (build.bat builds openbfme_peer)", file=sys.stderr)
        return 1
    profiles = [p for p in a.profiles.split(",") if p]
    unknown = [p for p in profiles if p not in PROFILES]
    if unknown:
        print(f"ERROR: unknown profile(s) {', '.join(unknown)}", file=sys.stderr)
        return 1
    out = a.out or ROOT / "workspace" / "fork" / "soak" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + a.hours * 3600 if a.hours else None
    count = a.games if a.games is not None else (None if deadline else len(GAMES))
    plan = itertools.cycle(itertools.product(range(len(GAMES)), profiles))
    results = []
    n = 0
    while (count is None or n < count) and (deadline is None or time.monotonic() < deadline):
        gi, profile = next(plan)
        game = GAMES[gi]
        seed = a.seed + n
        folder = out / f"game{n:04d}-{game[0]}-{profile}"
        # a game's whole time: the frames at full speed take a few minutes; a blackout and a lossy link add their stalls
        r = play(a.peer, game, profile, a.frames, seed, a.port + (n % 50) * 10, folder, timeout_s=max(900, a.frames // 2))
        results.append(r)
        with open(out / "results.jsonl", "a", encoding="utf-8") as f:
            f.write(json.dumps(r) + "\n")
        print(f"[{n + 1}] {game[0]} {profile} seed {seed}: {r['outcome']} in {r['seconds']} s{(': ' + r['why']) if r['why'] else ''}", flush=True)
        n += 1
    lines = summary(results)
    (out / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    worst = {r["outcome"] for r in results}
    return 2 if worst & {"desync", "crash"} else 3 if "network" in worst else 0


if __name__ == "__main__":
    sys.exit(main())

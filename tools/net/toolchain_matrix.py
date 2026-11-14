#!/usr/bin/env python3
"""Fork lane TOOLCHAIN-1 (plan phase 1, item 7): do the builds of different toolchains simulate the same game, frame for frame?

A lockstep game desyncs the moment two peers' builds compute one bit differently. The release ships one toolchain (llvm-mingw), contributors build
with MSVC (build.bat), Linux builds use GCC or Clang. This plays the same games with every build and compares every frame:
  1. every build records every game: openbfme_peer hosts it alone (a scripted human, the rest the logic's own skirmish AI, so nothing depends on
     timing) and writes the replay, which holds the state hash after every frame;
  2. the recordings' hashes are compared frame by frame (openbfme_peer --replay R --recorded-hashes): the first frame two builds differ on;
  3. every build plays every other build's recording back (--replay R --report): the re-simulation compares every recorded hash (the cross check
     that a replay, a rejoin or a spectator of a mixed game sees).
A difference names the game, the builds and the first frame; tools/replay/desync_bisect.py on the two recordings (with --peer / --peer-b) then
names the section, the object and its part.

    python tools/net/toolchain_matrix.py --peer msvc=engine/build/openbfme_peer.exe --peer llvm-mingw=build/windows/build/openbfme_peer.exe
        [--games N] [--frames N] [--out DIR]
Needs ROTWK_INSTALL / BFME2_INSTALL as openbfme_peer does. Exit 0: every build agrees on every frame of every game; 2: a difference; 1: an error.
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
import socket
import subprocess
import sys
import tempfile
from pathlib import Path

# the games: a map, the slots (the first is the host's scripted human; the AI difficulties as the lobby names them), the seed. 1v1 to 4v4,
# every faction at least once
GAMES = [
    ("2v2 fall back", "maps/map mp fall back 4p/map mp fall back 4p.map", 5,
     ["human,FactionMen,0,0", "medium,FactionMordor,1,1", "medium,FactionElves,2,0", "medium,FactionAngmar,3,1"]),
    ("4v4 fall back 8p", "maps/map mp fall back 8p/map mp fall back 8p.map", 11,
     ["human,FactionIsengard,0,0", "hard,FactionDwarves,1,1", "hard,FactionWild,2,0", "hard,FactionMen,3,1",
      "medium,FactionMordor,4,0", "medium,FactionElves,5,1", "brutal,FactionAngmar,6,0", "easy,FactionIsengard,7,1"]),
    ("1v1 fall back", "maps/map mp fall back 4p/map mp fall back 4p.map", 23,
     ["human,FactionWild,0,0", "brutal,FactionDwarves,1,1"]),
    ("3v3 fall back 8p", "maps/map mp fall back 8p/map mp fall back 8p.map", 37,
     ["human,FactionAngmar,0,0", "hard,FactionElves,1,1", "hard,FactionMordor,2,0", "medium,FactionMen,3,1",
      "medium,FactionIsengard,4,0", "hard,FactionWild,5,1"]),
]


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def read_hashes(text: str) -> dict[int, int]:
    out = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] != "final":
            out[int(parts[0])] = int(parts[1], 16)
    return out


def first_difference(a: dict[int, int], b: dict[int, int]) -> int | None:
    """The first frame either recorded where they differ (a frame only one of them recorded counts: a recording cut short is a difference)."""
    for frame in sorted(set(a) | set(b)):
        if a.get(frame) != b.get(frame):
            return frame
    return None


def report_fields(text: str) -> dict[str, str]:
    out = {}
    for line in text.splitlines():
        key, _, value = line.partition(" ")
        out.setdefault(key, value)
    return out


def run(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--peer", action="append", required=True, help="LABEL=PATH of an openbfme_peer build (two or more)")
    ap.add_argument("--games", type=int, default=len(GAMES), help=f"the first N of the {len(GAMES)} games")
    ap.add_argument("--frames", type=int, default=3000, help="logic frames per game (3000: ten minutes of game time)")
    ap.add_argument("--out", type=Path, default=None)
    a = ap.parse_args()
    peers = {}
    for spec in a.peer:
        label, _, path = spec.partition("=")
        if not label or not path or not Path(path).is_file():
            print(f"ERROR: --peer {spec}: LABEL=PATH of an existing openbfme_peer", file=sys.stderr)
            return 1
        peers[label] = Path(path).resolve()
    if len(peers) < 2:
        print("ERROR: two or more --peer builds to compare", file=sys.stderr)
        return 1
    out = a.out or Path(tempfile.mkdtemp(prefix="toolchain_matrix_"))
    out.mkdir(parents=True, exist_ok=True)
    games = GAMES[: a.games]
    results = {"frames": a.frames, "peers": {k: str(v) for k, v in peers.items()}, "games": []}
    worst = 0
    for gi, (name, map_key, seed, slots) in enumerate(games):
        print(f"== {name} (seed {seed}, {len(slots)} players, {a.frames} frames)", flush=True)
        game = {"name": name, "recordings": {}, "pairs": [], "cross": []}
        hashes = {}
        for label, exe in peers.items():
            replay = out / f"game{gi}-{label}.replay"
            cmd = [str(exe), "--frames", str(a.frames), "--map", map_key, "--seed", str(seed), "--script", "--crc-interval", "10"]
            for s in slots:
                cmd += ["--slot", s]
            cmd += ["--host", str(free_port()), "--record", str(replay), "--report", str(out / f"game{gi}-{label}.txt")]
            r = run(cmd, out)
            if r.returncode != 0:
                print(f"ERROR: {label} could not record {name} (exit {r.returncode}): {(r.stderr or r.stdout).strip()[-400:]}", file=sys.stderr)
                return 1
            h = run([str(exe), "--replay", str(replay), "--recorded-hashes", str(out / f"game{gi}-{label}.hashes")], out)
            if h.returncode != 0:
                print(f"ERROR: {label} --recorded-hashes: {(h.stderr or h.stdout).strip()}", file=sys.stderr)
                return 1
            hashes[label] = read_hashes((out / f"game{gi}-{label}.hashes").read_text(encoding="utf-8"))
            game["recordings"][label] = {"replay": str(replay), "frames_hashed": len(hashes[label])}
            print(f"   {label}: recorded, {len(hashes[label])} frame hashes", flush=True)
        for x, y in itertools.combinations(peers, 2):
            diff = first_difference(hashes[x], hashes[y])
            game["pairs"].append({"a": x, "b": y, "first_difference": diff})
            print(f"   {x} vs {y}: {'every frame equal' if diff is None else f'DIFFER from frame {diff}'}", flush=True)
            if diff is not None:
                worst = 2
                print(f"     python tools/replay/desync_bisect.py --replay-a {out / f'game{gi}-{x}.replay'} --replay-b {out / f'game{gi}-{y}.replay'} "
                      f"--peer {peers[x]} --peer-b {peers[y]}", flush=True)
        for recorder, player in itertools.permutations(peers, 2):
            replay = out / f"game{gi}-{recorder}.replay"
            rep = out / f"game{gi}-{recorder}-played-by-{player}.txt"
            r = run([str(peers[player]), "--replay", str(replay), "--report", str(rep)], out)
            f = report_fields(rep.read_text(encoding="utf-8", errors="replace")) if rep.is_file() else {}
            entry = {"recorded_by": recorder, "played_by": player, "exit": r.returncode, "hashes_compared": f.get("hashes_compared"),
                     "mismatches": f.get("mismatches"), "first_mismatch": f.get("first_mismatch")}
            game["cross"].append(entry)
            ok = r.returncode == 0 and f.get("mismatches") == "0"
            worst = worst if ok else 2
            print(f"   {player} plays {recorder}'s recording: {f.get('hashes_compared', '?')} hashes compared, {f.get('mismatches', '?')} mismatches"
                  + ("" if ok else f" (exit {r.returncode}; {f.get('first_mismatch', 'see ' + str(rep))})"), flush=True)
        results["games"].append(game)
    (out / "matrix.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(f"{'every build agrees on every frame' if worst == 0 else 'THE BUILDS DIFFER'} ({len(games)} games, {len(peers)} builds; {out})")
    return worst


if __name__ == "__main__":
    sys.exit(main())

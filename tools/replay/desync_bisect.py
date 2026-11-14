#!/usr/bin/env python3
"""Fork lane DESYNC-1 (plan phase 1, item 4): from two peers' replays of one game to the first frame, object and part of an object they disagree on.

Every network game is recorded by every peer, and each recording holds that peer's own state hash after every logic frame (Common/Recorder.h). So
the first frame two peers disagreed on is in their replays, with nothing re-run:
  1. openbfme_peer --replay A --recorded-hashes, the same for B: the first frame both recorded whose hashes differ (the divergence);
  2. openbfme_peer --replay A --breakdown-at F (and B, with --peer-b when B came from another build or toolchain): each re-simulation stops after
     frame F and writes the state hash broken down to every section's and every object's words, with the parts of each object (its own fields,
     every module by its INI tag, its combat state; Object::crcSpans). The report says whether the re-simulation reproduced that peer's recorded
     hash: a breakdown that does not is not the peer's state, and the result says so;
  3. the two breakdowns compared: the sections that differ (and the first word that does), the objects only one side has, and for every object
     that differs the part and the word index where it starts to.
A planted desync (openbfme_peer --inject-divergence F: one more unit of money on one peer) is found at frame F in the "players and teams" section;
--inject-b F makes the planted change again when B's recording is re-simulated (it is no command, so the replay alone cannot reproduce it);
--inject-object-divergence F / --inject-object-b F plant and replay an object's desync (its position: the part "object").
The Uber profile keeps no words of its hash: its games are bisected to the frame and the differing objects only (step 2 fails with a message).

    python tools/replay/desync_bisect.py --replay-a A.replay --replay-b B.replay [--peer EXE] [--peer-b EXE] [--out DIR] [--max-objects N]
    python tools/replay/desync_bisect.py --dump DESYNC-Frame<N>-<exe>-<player>.txt
The second form reads one player's desync report (GameNetwork/DesyncDump.h, the file the game writes into its desyncs folder): it holds every
peer's sections and object hashes of the first differing frame, so it names the sections and the objects that differ without any replay (not the
part of an object: the words are not in it).
Needs ROTWK_INSTALL / BFME2_INSTALL as openbfme_peer does. Exit 0: a divergence was found and broken down; 3: the replays agree on every frame both
recorded; 2: a re-simulation did not reproduce its recording (the breakdown of that side is not the peer's state); 1: an error.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PEER = ROOT / "engine" / "build" / ("openbfme_peer.exe" if os.name == "nt" else "openbfme_peer")


def read_hashes(text: str) -> tuple[dict[int, int], int | None]:
    """--recorded-hashes output: {frame: hash} and the recording's final frame (None when the line is missing)."""
    hashes: dict[int, int] = {}
    final = None
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == "final":
            final = int(parts[1])
        elif len(parts) == 2:
            hashes[int(parts[0])] = int(parts[1], 16)
    return hashes, final


def first_divergence(a: dict[int, int], b: dict[int, int]) -> int | None:
    """The first frame both recorded whose hashes differ."""
    for frame in sorted(set(a) & set(b)):
        if a[frame] != b[frame]:
            return frame
    return None


def parse_breakdown(text: str) -> dict:
    """--breakdown-out output (openbfme_peer BreakdownDriver::describe)."""
    out: dict = {"frame": None, "total": None, "sections": [], "objects": {}}
    words_of: dict[int, list[int]] = {}
    for line in text.splitlines():
        kind, _, rest = line.partition(" ")
        if kind == "frame":
            out["frame"] = int(rest)
        elif kind == "total":
            out["total"] = int(rest, 16)
        elif kind == "section":
            index, value, name = rest.split(" ", 2)
            out["sections"].append({"index": int(index), "value": int(value, 16), "name": name, "words": []})
        elif kind == "words":
            fields = rest.split()
            words_of[int(fields[0])] = [int(w, 16) for w in fields[1:]]
        elif kind == "object":
            oid, value, *template = rest.split(" ", 2)
            out["objects"][int(oid)] = {"value": int(value, 16), "template": template[0] if template else "", "spans": [], "words": []}
        elif kind == "span":
            oid, offset, part = rest.split(" ", 2)
            out["objects"][int(oid)]["spans"].append((int(offset), part))
        elif kind == "owords":
            fields = rest.split()
            out["objects"][int(fields[0])]["words"] = [int(w, 16) for w in fields[1:]]
    for s in out["sections"]:
        s["words"] = words_of.get(s["index"], [])
    return out


def first_word_difference(a: list[int], b: list[int]) -> int | None:
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return i
    return None if len(a) == len(b) else min(len(a), len(b))


def part_at(spans: list[tuple[int, str]], index: int) -> str:
    """The object part a word index falls in (the last span starting at or before it)."""
    name = spans[0][1] if spans else "object"
    for offset, part in spans:
        if offset <= index:
            name = part
    return name


def diff_breakdowns(a: dict, b: dict, max_objects: int = 20) -> dict:
    sections = []
    for sa, sb in zip(a["sections"], b["sections"]):
        if sa["name"] != sb["name"]:
            sections.append({"name": f"{sa['name']} / {sb['name']}", "why": "the sections differ in order (another build?)"})
            break
        if sa["value"] != sb["value"]:
            entry = {"name": sa["name"], "a": f"{sa['value']:08x}", "b": f"{sb['value']:08x}"}
            if sa["words"] or sb["words"]:
                w = first_word_difference(sa["words"], sb["words"])
                entry.update({"first_word": w, "words_a": len(sa["words"]), "words_b": len(sb["words"])})
                if w is not None and w < len(sa["words"]) and w < len(sb["words"]):
                    entry.update({"word_a": f"{sa['words'][w]:08x}", "word_b": f"{sb['words'][w]:08x}"})
            sections.append(entry)
    if len(a["sections"]) != len(b["sections"]):
        sections.append({"name": "(count)", "why": f"{len(a['sections'])} sections against {len(b['sections'])}"})
    ids_a, ids_b = set(a["objects"]), set(b["objects"])
    objects = []
    for oid in sorted(ids_a & ids_b):
        oa, ob = a["objects"][oid], b["objects"][oid]
        if oa["value"] == ob["value"]:
            continue
        w = first_word_difference(oa["words"], ob["words"])
        entry = {"id": oid, "template": oa["template"], "a": f"{oa['value']:08x}", "b": f"{ob['value']:08x}"}
        if w is not None:
            entry.update({"first_word": w, "part": part_at(oa["spans"], w)})
            if w < len(oa["words"]) and w < len(ob["words"]):
                entry.update({"word_a": f"{oa['words'][w]:08x}", "word_b": f"{ob['words'][w]:08x}"})
        objects.append(entry)
    return {
        "frame": a["frame"],
        "total_a": f"{a['total']:08x}" if a["total"] is not None else None,
        "total_b": f"{b['total']:08x}" if b["total"] is not None else None,
        "sections": sections,
        "objects_differing": len(objects),
        "objects": objects[:max_objects],
        "only_a": [{"id": i, "template": a["objects"][i]["template"]} for i in sorted(ids_a - ids_b)][:max_objects],
        "only_b": [{"id": i, "template": b["objects"][i]["template"]} for i in sorted(ids_b - ids_a)][:max_objects],
    }


def summary_lines(d: dict) -> list[str]:
    lines = [f"first divergence: logic frame {d['frame']} (state hash {d['total_a']} against {d['total_b']})"]
    for s in d["sections"]:
        if "why" in s:
            lines.append(f"  section {s['name']}: {s['why']}")
            continue
        at = f", first at word {s['first_word']} ({s.get('word_a', '-')} against {s.get('word_b', '-')})" if s.get("first_word") is not None else ""
        lines.append(f"  section {s['name']}: {s['a']} against {s['b']}{at}")
    lines.append(f"  objects that differ: {d['objects_differing']}")
    for o in d["objects"]:
        at = f": {o['part']}, word {o['first_word']} ({o.get('word_a', '-')} against {o.get('word_b', '-')})" if "part" in o else ""
        lines.append(f"    object {o['id']} {o['template']}{at}")
    for side in ("only_a", "only_b"):
        for o in d[side]:
            lines.append(f"    object {o['id']} {o['template']}: exists in {side[-1].upper()} only")
    return lines


def parse_dump(text: str) -> tuple[int | None, dict[int, dict]]:
    """A DesyncDump file: the frame and, per slot, a breakdown as parse_breakdown returns it (sections and objects without words)."""
    halves: dict[int, dict] = {}
    current = None
    frame = None
    for line in text.splitlines():
        if line.startswith("slot ") and "(frame " in line:
            slot = int(line.split()[1])
            frame = int(line.split("(frame ")[1].split(",")[0])
            current = halves[slot] = {"frame": frame, "total": int(line.rsplit("hash ", 1)[1].rstrip(")"), 16), "sections": [], "objects": {}}
        elif current is not None and line.startswith("section "):
            name, value = line[len("section "):].rsplit(" ", 1)
            current["sections"].append({"index": len(current["sections"]), "value": int(value, 16), "name": name, "words": []})
        elif current is not None and line.startswith("object "):
            fields = line.split()
            current["objects"][int(fields[1])] = {"value": int(fields[-1], 16), "template": " ".join(fields[2:-1]), "spans": [], "words": []}
        elif line.startswith("REPLAY FILE"):
            current = None  # the replay section: no more half lines
    return frame, halves


def main_dump(path: Path, max_objects: int) -> int:
    frame, halves = parse_dump(path.read_text(encoding="utf-8", errors="replace"))
    if len(halves) < 2:
        print(f"{path}: {len(halves)} peer half in the report (the others had not arrived when it was written): nothing to compare", file=sys.stderr)
        return 1
    slots = sorted(halves)
    worst = 3
    for other in slots[1:]:
        d = diff_breakdowns(halves[slots[0]], halves[other], max_objects)
        print(f"slot {slots[0]} (A) against slot {other} (B):")
        print("\n".join(summary_lines(d)))
        worst = 0 if d["sections"] or d["objects_differing"] or d["only_a"] or d["only_b"] else worst
    return worst


def peer(exe: Path, args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run([str(exe), *args], capture_output=True, text=True, encoding="utf-8", errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--replay-a", type=Path)
    ap.add_argument("--replay-b", type=Path)
    ap.add_argument("--dump", type=Path, help="one player's desync report instead of two replays")
    ap.add_argument("--peer", type=Path, default=DEFAULT_PEER, help="the openbfme_peer that recorded A (and B, unless --peer-b)")
    ap.add_argument("--peer-b", type=Path, default=None, help="the openbfme_peer of B's build, when it differs")
    ap.add_argument("--out", type=Path, default=None, help="where the breakdowns and the report go (default: a new temporary folder)")
    ap.add_argument("--max-objects", type=int, default=20)
    ap.add_argument("--inject-b", type=int, default=None, help="B is a peer run with --inject-divergence FRAME (a test of the bisector)")
    ap.add_argument("--inject-object-b", type=int, default=None, help="B is a peer run with --inject-object-divergence FRAME (a test of the bisector)")
    a = ap.parse_args()
    if a.dump:
        return main_dump(a.dump, a.max_objects)
    if not a.replay_a or not a.replay_b:
        ap.error("--replay-a and --replay-b (or --dump)")
    peer_b = a.peer_b or a.peer
    for exe in {a.peer, peer_b}:
        if not exe.is_file():
            print(f"ERROR: {exe} not found (build.bat builds openbfme_peer)", file=sys.stderr)
            return 1
    out = a.out or Path(tempfile.mkdtemp(prefix="desync_bisect_"))
    out.mkdir(parents=True, exist_ok=True)
    recorded = {}
    for side, replay, exe in (("a", a.replay_a, a.peer), ("b", a.replay_b, peer_b)):
        path = out / f"hashes-{side}.txt"
        r = peer(exe, ["--replay", str(replay), "--recorded-hashes", str(path)])
        if r.returncode != 0:
            print(f"ERROR: {exe.name} --recorded-hashes of {replay}: {r.stderr.strip() or r.stdout.strip()}", file=sys.stderr)
            return 1
        recorded[side] = read_hashes(path.read_text(encoding="utf-8"))
    (ha, final_a), (hb, final_b) = recorded["a"], recorded["b"]
    frame = first_divergence(ha, hb)
    common = len(set(ha) & set(hb))
    if frame is None:
        print(f"the replays agree on all {common} frames both recorded (A ends at {final_a}, B at {final_b})")
        return 3
    print(f"first divergence at logic frame {frame} ({common} frames both recorded); breaking both states down")
    breakdowns = {}
    reproduced = True
    for side, replay, exe in (("a", a.replay_a, a.peer), ("b", a.replay_b, peer_b)):
        path = out / f"breakdown-{side}-{frame}.txt"
        planted = []
        if side == "b" and a.inject_b is not None:
            planted += ["--inject-divergence", str(a.inject_b)]
        if side == "b" and a.inject_object_b is not None:
            planted += ["--inject-object-divergence", str(a.inject_object_b)]
        r = peer(exe, ["--replay", str(replay), "--breakdown-at", str(frame), "--breakdown-out", str(path), *planted])
        if r.returncode != 0:
            print(f"ERROR: {exe.name} --breakdown-at {frame} of {replay}: {r.stderr.strip() or r.stdout.strip()}", file=sys.stderr)
            return 1
        if "recorded same" not in r.stdout:
            reproduced = False
            print(f"WARNING: the re-simulation of {side.upper()} did not reproduce its recorded hash at frame {frame} ({r.stdout.strip()}): its breakdown is "
                  f"this build's state, not that peer's (pass --peer{'-b' if side == 'b' else ''} the build that recorded it)")
        breakdowns[side] = parse_breakdown(path.read_text(encoding="utf-8"))
    d = diff_breakdowns(breakdowns["a"], breakdowns["b"], a.max_objects)
    d["reproduced"] = reproduced
    lines = summary_lines(d)
    (out / "report.json").write_text(json.dumps(d, indent=2), encoding="utf-8")
    (out / "report.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    print(f"(breakdowns and report in {out})")
    return 0 if reproduced else 2


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Fork lane MP-0: how far the fork is from upstream (Open-BFME/openbfme-godot), and whether a merge would conflict, without changing anything.

Upstream ships nightly and most gameplay parity arrives from there, so the fork merges it often. A scheduled GitHub workflow that merges and pushes
would need write access, which tools/release/public_workflows.py refuses; this runs locally instead:
  1. git fetch <remote> (default upstream);
  2. the commits the fork lacks (<remote>/main not in HEAD) and the fork's own (HEAD not in <remote>/main);
  3. a trial merge in memory (git merge-tree --write-tree, git 2.38+): the paths that would conflict. The working tree, the index and every branch
     stay as they are.
With --merge and no conflicts it merges <remote>/main into the current branch (a merge commit, the pre-commit gate runs); nothing is pushed.

    python tools/fork/upstream_sync.py [--remote upstream] [--branch main] [--merge]
Exit 0: up to date or a clean merge is possible (or was made); 2: the merge would conflict (the paths are listed); 1: an error.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def git(*args: str, check: bool = True) -> subprocess.CompletedProcess:
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check and r.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {r.stderr.strip() or r.stdout.strip()}")
    return r


def conflicts(merge_tree_output: str) -> list[str]:
    """The conflicted paths of `git merge-tree --write-tree --name-only` (the lines after the tree id, up to the first blank line)."""
    lines = merge_tree_output.splitlines()[1:]
    out = []
    for line in lines:
        if not line.strip():
            break
        if line not in out:
            out.append(line)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--remote", default="upstream")
    ap.add_argument("--branch", default="main")
    ap.add_argument("--merge", action="store_true", help="merge when it is clean (never pushes)")
    a = ap.parse_args()
    target = f"{a.remote}/{a.branch}"
    try:
        git("fetch", a.remote)
        behind = git("rev-list", "--count", f"HEAD..{target}").stdout.strip()
        ahead = git("rev-list", "--count", f"{target}..HEAD").stdout.strip()
        print(f"{target}: the fork lacks {behind} commit(s), has {ahead} of its own")
        if behind == "0":
            print("up to date")
            return 0
        for line in git("log", "--oneline", "--no-decorate", "-n", "15", f"HEAD..{target}").stdout.splitlines():
            print(f"  {line}")
        if int(behind) > 15:
            print(f"  ... and {int(behind) - 15} more")
        trial = git("merge-tree", "--write-tree", "--name-only", "HEAD", target, check=False)
        if trial.returncode not in (0, 1):
            raise RuntimeError(f"git merge-tree: {trial.stderr.strip()}")
        if trial.returncode == 1:
            paths = conflicts(trial.stdout)
            print(f"a merge would conflict in {len(paths)} path(s):")
            for p in paths:
                print(f"  {p}")
            return 2
        print("a merge is clean")
        if a.merge:
            if git("status", "--porcelain", "--untracked-files=no").stdout.strip():
                print("not merged: the working tree has uncommitted changes (commit or stash them first)", file=sys.stderr)
                return 1
            git("merge", "--no-ff", "-m", f"Merge {target} into the fork", target)
            print(f"merged {target} (not pushed)")
        return 0
    except RuntimeError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

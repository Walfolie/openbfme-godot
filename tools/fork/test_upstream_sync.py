"""Fork lane MP-0: tools/fork/upstream_sync.py reads git merge-tree's conflicted paths (git 2.38+ --write-tree --name-only output)."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("upstream_sync", Path(__file__).with_name("upstream_sync.py"))
us = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(us)


def test_conflicted_paths_up_to_the_messages():
    out = "4b825dc642cb6eb9a060e54bf8d69288fbee4904\nengine/a.cpp\nengine/a.cpp\ndocs/b.md\n\nAuto-merging engine/a.cpp\nCONFLICT (content): Merge conflict in engine/a.cpp\n"
    assert us.conflicts(out) == ["engine/a.cpp", "docs/b.md"]


def test_clean_merge_has_no_paths():
    assert us.conflicts("4b825dc642cb6eb9a060e54bf8d69288fbee4904\n") == []

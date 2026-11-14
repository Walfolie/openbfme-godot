"""Fork lane TOOLCHAIN-1: tools/net/toolchain_matrix.py's comparison of two builds' recorded hashes and its reading of a replay report."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("toolchain_matrix", Path(__file__).with_name("toolchain_matrix.py"))
tm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tm)


def test_the_first_difference_counts_a_frame_only_one_build_recorded():
    a = tm.read_hashes("1 0000000a\n2 0000000b\n3 0000000c\nfinal 3 complete\n")
    assert a == {1: 10, 2: 11, 3: 12}
    assert tm.first_difference(a, dict(a)) is None
    assert tm.first_difference(a, {1: 10, 2: 99, 3: 12}) == 2
    assert tm.first_difference(a, {1: 10, 2: 11}) == 3  # a recording cut short differs where it stops


def test_the_replay_report_fields():
    f = tm.report_fields("hashes_compared 3000\nmismatches 0\nfirst_mismatch frame 7 recorded 0x1 played 0x2\n")
    assert f["hashes_compared"] == "3000" and f["mismatches"] == "0"
    assert f["first_mismatch"].startswith("frame 7")


def test_every_faction_plays():
    factions = {s.split(",")[1] for _, _, _, slots in tm.GAMES for s in slots}
    assert factions == {"FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar"}
    assert sorted(len(slots) for _, _, _, slots in tm.GAMES) == [2, 4, 6, 8]  # 1v1 .. 4v4

"""Fork lane SOAK-1: tools/net/soak.py's judgement of one game from its peers' exits, reports and per-frame hashes."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("soak", Path(__file__).with_name("soak.py"))
soak = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(soak)


def peer(rc=0, frames="300", desyncs="0", hashes=None, planted=False, report=True):
    return {"rc": rc, "planted_stop": planted, "report": {"frames": frames, "desyncs": desyncs} if report else None,
            "hashes": hashes if hashes is not None else {1: "0x1", 2: "0x2"}}


def test_a_clean_game_is_ok():
    assert soak.judge([peer(), peer()], 300) == ("ok", "")


def test_a_planted_crash_is_ok_when_the_others_finish():
    assert soak.judge([peer(), peer(), peer(frames="", hashes={1: "0x1"}, planted=True)], 300)[0] == "ok"


def test_desync_crash_and_network():
    assert soak.judge([peer(), peer(rc=2, desyncs="3")], 300)[0] == "desync"
    assert soak.judge([peer(), peer(hashes={1: "0x1", 2: "0xBAD"})], 300) == ("desync", "frame 2: the peers' hashes differ (0x2, 0xBAD)")
    assert soak.judge([peer(), peer(rc=-1073741819)], 300)[0] == "crash"  # an access violation's exit code
    assert soak.judge([peer(), peer(report=False)], 300)[0] == "crash"
    assert soak.judge([peer(), peer(rc=3)], 300)[0] == "network"
    assert soak.judge([peer(), peer(frames="120")], 300)[0] == "network"


def test_the_table_covers_every_faction_and_size():
    factions = {s.split(",")[1] for _, _, slots in soak.GAMES for s in slots}
    assert len(factions) == 7
    assert {name for name, _, _ in soak.GAMES} == {"1v1", "2v2", "3v3", "4v4"}
    assert max(sum(s.startswith("human,") for s in slots) for _, _, slots in soak.GAMES) == 4  # at most four peer processes

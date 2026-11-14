"""Fork lane DESYNC-1: tools/replay/desync_bisect.py reads openbfme_peer's --recorded-hashes and --breakdown-out output and names the first frame, the
sections and the objects (with the part of each) two peers disagree on."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("desync_bisect", Path(__file__).with_name("desync_bisect.py"))
db = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(db)


def breakdown(money: str, hp: str, extra_object: bool = False) -> str:
    lines = [
        "frame 150",
        "total 0badc0de" if money == "00000064" else "total 12345678",
        "section 0 00000011 frame, object ids, RNG",
        "words 0 00000096 00000001",
        "section 1 00000022 objects",
        "words 1",
        f"section 2 {money} players and teams",
        f"words 2 00000002 {money} 00000007",
        "object 7 aaaaaaaa GondorFighter" if hp == "42c80000" else "object 7 abababab GondorFighter",
        "span 7 0 object",
        "span 7 3 module ModuleTag_Body",
        "span 7 5 combat",
        f"owords 7 00000007 00000001 00000000 00000009 {hp} 00000000",
        "object 8 bbbbbbbb MordorOrc",
        "span 8 0 object",
        "owords 8 00000008",
    ]
    if extra_object:
        lines += ["object 9 dddddddd MordorTroll", "span 9 0 object", "owords 9 00000009"]
    return "\n".join(lines) + "\n"


def test_recorded_hashes_and_the_first_divergence():
    a, final = db.read_hashes("148 00000001\n149 00000002\n150 00000003\n151 00000004\nfinal 400 complete\n")
    b, _ = db.read_hashes("149 00000002\n150 0000000f\n151 00000004\nfinal 380 incomplete\n")
    assert final == 400
    assert db.first_divergence(a, b) == 150  # 148 is A's only; the later agreement at 151 does not hide 150
    assert db.first_divergence(a, a) is None


def test_the_breakdown_diff_names_the_section_word_and_the_object_part():
    a = db.parse_breakdown(breakdown("00000064", "42c80000"))
    b = db.parse_breakdown(breakdown("00000065", "42c60000", extra_object=True))
    assert a["frame"] == 150 and a["objects"][7]["spans"][1] == (3, "module ModuleTag_Body")
    d = db.diff_breakdowns(a, b)
    assert d["frame"] == 150
    assert [s["name"] for s in d["sections"]] == ["players and teams"]
    assert d["sections"][0]["first_word"] == 1 and d["sections"][0]["word_a"] == "00000064" and d["sections"][0]["word_b"] == "00000065"
    assert d["objects_differing"] == 1  # object 8 is the same on both sides
    o = d["objects"][0]
    assert (o["id"], o["template"], o["part"], o["first_word"]) == (7, "GondorFighter", "module ModuleTag_Body", 4)
    assert (o["word_a"], o["word_b"]) == ("42c80000", "42c60000")  # 100.0f against 99.0f
    assert [o["id"] for o in d["only_b"]] == [9]
    text = "\n".join(db.summary_lines(d))
    assert "section players and teams" in text and "exists in B only" in text


def test_an_object_part_by_word_index():
    spans = [(0, "object"), (3, "module ModuleTag_Body"), (5, "combat")]
    assert db.part_at(spans, 0) == "object"
    assert db.part_at(spans, 4) == "module ModuleTag_Body"
    assert db.part_at(spans, 5) == "combat"
    assert db.first_word_difference([1, 2, 3], [1, 2, 4]) == 2
    assert db.first_word_difference([1, 2], [1, 2, 3]) == 2
    assert db.first_word_difference([1, 2], [1, 2]) is None


DUMP = """Frame #152

local slot 0
DESYNC: the state hash of logic frame 150 differs (CRCs compared on frame 152)

---------------------------------------------------------
slot 0 (frame 150, hash 0x444A4F92)
section frame, object ids, RNG 0x00000011
section players and teams 0x45A060C5
object 7 GondorFighter 0xAAAAAAAA
object 179 MordorFortress 0xEFB1720E

---------------------------------------------------------
slot 1 (frame 150, hash 0xF2485945)
section frame, object ids, RNG 0x00000011
section players and teams 0x45A060C5
object 7 GondorFighter 0xAAAAAAAA
object 179 MordorFortress 0x8C905C16

--------------------------------------------------------
REPLAY FILE
---------------------------------------------------------
C:/x/A.replay (copied to C:/x/DESYNC-Frame152.replay)
---------------------------------------------------------
"""


def test_one_players_desync_report_names_the_objects():
    frame, halves = db.parse_dump(DUMP)
    assert frame == 150 and sorted(halves) == [0, 1]
    assert halves[0]["sections"][0]["name"] == "frame, object ids, RNG"
    d = db.diff_breakdowns(halves[0], halves[1])
    assert d["sections"] == []
    assert [(o["id"], o["template"]) for o in d["objects"]] == [(179, "MordorFortress")]

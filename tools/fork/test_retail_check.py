"""Fork lane MP-0: tools/fork/retail_check.py reads the unit test log as the tests print it (RetailTestMount.cpp's SKIP lines, doctest's summary)."""
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("retail_check", Path(__file__).with_name("retail_check.py"))
rc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rc)

LOG = r"""
C:\src\engine\tests\test_hud6.cpp(787): FATAL ERROR: REQUIRE( pu->isUpgradeInQueue(u) ) is NOT correct!
C:\src\engine\tests\test_a.cpp(12): ERROR: CHECK( x == 1 ) is NOT correct!
C:\src\engine\tests\test_a.cpp(12): ERROR: CHECK( x == 1 ) is NOT correct!
SKIP: ui4 retail: the English language.ini's values is pinned to the English retail data; the install is German: not run
SKIP: stealth binary facts (RW_GAME_DAT unset): needs the variables it names (RW_GAME_DAT = the path of RotWK's game.dat); not run
SKIP: SDK-1 asset pack corpus (OPENBFME_ASSET_PACK unset) needs ROTWK_INSTALL and BFME2_INSTALL (retail files); not run
C:\src\engine\tests\test_sdk0_packs.cpp(151): MESSAGE: SKIP: this file system cannot make symbolic links: A required privilege is not held by the client.
SKIP: hud5 icons needs ROTWK_INSTALL and BFME2_INSTALL (retail files); not run
[doctest] test cases:   1157 |   1155 passed | 2 failed | 4382 skipped
"""


def test_summary_and_failures():
    r = rc.parse_unit_log(LOG, set())
    assert r["cases"] == {"total": 1157, "passed": 1155, "failed": 2, "skipped": 4382}
    assert r["failures"] == ["test_a.cpp:12: CHECK( x == 1 ) is NOT correct!", "test_hud6.cpp:787: REQUIRE( pu->isUpgradeInQueue(u) ) is NOT correct!"]


def test_skips_with_every_input_set():
    r = rc.parse_unit_log(LOG, set())
    assert [s.split(":")[0] for s in r["skips"]["language"]] == ["ui4 retail"]
    # RW_GAME_DAT is set: a test that still names it as unset is missing data; the asset pack is an optional input, symbolic links the file system
    assert [s.split(" (")[0] for s in r["skips"]["data"]] == ["stealth binary facts"]
    assert len(r["skips"]["other"]) == 3


def test_skips_without_the_installs():
    r = rc.parse_unit_log(LOG, {"ROTWK_INSTALL", "BFME2_INSTALL", "RW_GAME_DAT"})
    assert any(s.startswith("hud5 icons") for s in r["skips"]["data"])
    assert not any(s.startswith("SDK-1") for s in r["skips"]["data"])  # it names its own optional input


def test_no_summary_is_reported_as_none():
    assert rc.parse_unit_log("crashed\n", set())["cases"] is None

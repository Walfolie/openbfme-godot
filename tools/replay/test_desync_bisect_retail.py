"""Fork lane DESYNC-1, the acceptance run (needs ROTWK_INSTALL / BFME2_INSTALL and a built openbfme_peer; skipped without them, so not in the public
pull request subset): two peers play a game, the joiner moves its first object 1/16 unit after frame 150 (--inject-object-divergence), both record;
tools/replay/desync_bisect.py on the two replays must name frame 150, that object and its part "object", with both re-simulations reproducing their
recordings. Measured 2026-11-14: object 179 MordorFortress, word 135 (x 1770.0 against 1770.0625); about two minutes."""
import os
import socket
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
PEER = ROOT / "engine" / "build" / ("openbfme_peer.exe" if os.name == "nt" else "openbfme_peer")
BISECT = Path(__file__).with_name("desync_bisect.py")
FRAME = 150

pytestmark = pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL") and PEER.is_file()),
                                reason="needs ROTWK_INSTALL, BFME2_INSTALL and engine/build/openbfme_peer")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def test_a_planted_object_desync_is_named_by_frame_object_and_part(tmp_path):
    port = free_port()
    common = [str(PEER), "--frames", "300", "--map", "maps/map mp fall back 4p/map mp fall back 4p.map", "--seed", "77", "--script",
              "--crc-interval", "10", "--run-ahead", "2", "--cash", "10000"]
    host = subprocess.Popen(common + ["--slot", "human,FactionMen,0,0", "--slot", "human,FactionMordor,1,1", "--host", str(port),
                                      "--record", str(tmp_path / "A.replay")], cwd=tmp_path, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    join = subprocess.run(common + ["--join", f"127.0.0.1:{port}", "--name", "Joiner1", "--inject-object-divergence", str(FRAME),
                                    "--record", str(tmp_path / "B.replay")], cwd=tmp_path, capture_output=True, text=True, timeout=900)
    host.wait(timeout=900)
    assert join.returncode == 2  # the peers found the desync themselves
    r = subprocess.run([sys.executable, str(BISECT), "--replay-a", str(tmp_path / "A.replay"), "--replay-b", str(tmp_path / "B.replay"),
                        "--inject-object-b", str(FRAME), "--out", str(tmp_path / "bisect")], capture_output=True, text=True, timeout=900)
    assert r.returncode == 0, r.stdout + r.stderr  # found, and both re-simulations reproduced their recordings
    report = (tmp_path / "bisect" / "report.txt").read_text(encoding="utf-8")
    assert report.startswith(f"first divergence: logic frame {FRAME} ")
    assert "objects that differ: 1\n" in report
    assert "MordorFortress: object, word" in report  # the joiner's (Mordor's) first object, in its own fields (the position)

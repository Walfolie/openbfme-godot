"""Fork lane NETGUARD-1: no bound GameWorld method may change the simulation outside the command path in a network game.

A lockstep game stays in sync only while every peer runs the same commands. A GameWorld method that writes the live logic directly (creates or
destroys an object, deals damage, pays money, grants experience, readies a power, moves an object) runs on one peer only, so in a network game it
is a desync, and from GDScript a cheat. The test hooks already refuse there (GodotGameWorld.cpp's debug_* methods); the scenario helpers
create_object, destroy_object, build2_damage, ready_object_powers, gain_hero_levels, kill_hero, damage_object and give_money did not.
This test reads the real code: every GameWorld member defined under engine/src/GodotDevice whose body makes such a write must test the network
state (m_netHost || m_netClient || m_netStarted || m_netSession, or hooksRefused with it) before its first write.
It fails on the code before NETGUARD-1 (the eight helpers above).
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEVICE = ROOT / "engine" / "src" / "GodotDevice"

DEF = re.compile(r"^[\w:<>,\s\*&]*\bGameWorld::(\w+)\([^;{]*\)\s*(?:const\s*)?\{", re.M)
# direct writes of the live simulation (outside GameMessage / commands().append, which the network carries to every peer)
MUTATION = re.compile(
    r"\b(depositMoney|withdrawMoney|attemptDamage|setReadyFrame|gainExpForLevel|addUpgrade|giveUpgrade|setControllingPlayer|setTeam)\(|"
    r"(->|\.)(destroyObject|createObject|setPosition|kill)\(")
NET_GUARD = re.compile(r"m_netHost \|\| m_netClient \|\| m_netStarted \|\| m_netSession")
# the helpers NETGUARD-1 guarded: each must stay found as a mutator (so the scan keeps seeing them) and guarded
GUARDED_BY_NETGUARD1 = {"create_object", "destroy_object", "build2_damage", "ready_object_powers", "gain_hero_levels", "kill_hero",
                        "damage_object", "give_money"}


def cpp_body(source: str, start: int) -> str:
    body = source.index("{", start)
    depth = 0
    for i in range(body, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[body:i + 1]
    raise AssertionError("unterminated body")


def strip_comments(body: str) -> str:
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return re.sub(r"//[^\n]*", "", body)


def mutators() -> dict:
    """{method: (file, body)} of the GameWorld members that write the live simulation directly"""
    out = {}
    for path in sorted(DEVICE.glob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        for m in DEF.finditer(text):
            body = strip_comments(cpp_body(text, m.start()))
            if MUTATION.search(body):
                out[m.group(1)] = (path.name, body)
    return out


def test_scan_finds_the_guarded_helpers():
    found = mutators()
    missing = sorted(GUARDED_BY_NETGUARD1 - set(found))
    assert not missing, f"the scan no longer sees these mutators (renamed? the MUTATION pattern is stale): {missing}"


def test_every_mutator_refuses_a_network_game_before_writing():
    unguarded = []
    for name, (file, body) in sorted(mutators().items()):
        guard = NET_GUARD.search(body)
        write = MUTATION.search(body)
        if not guard or guard.start() > write.start():
            unguarded.append(f"{file}: GameWorld::{name}")
    assert not unguarded, "these GameWorld methods write the simulation without refusing a network game first:\n  " + "\n  ".join(unguarded)


def test_guard_pattern_matches_the_old_code_shape():
    # the scan's own check: the old create_object (no guard before createObject) is reported, the guarded one is not
    old = '{\n\tif (!m_game)\n\t{\n\t\treturn -1;\n\t}\n\t::Object *o = m_game->createObject(n, p, pos, a, &e);\n}'
    new = '{\n\tif (m_netHost || m_netClient || m_netStarted || m_netSession)\n\t{\n\t\treturn -1;\n\t}\n\t::Object *o = m_game->createObject(n, p, pos, a, &e);\n}'
    assert MUTATION.search(old) and not NET_GUARD.search(old)
    assert NET_GUARD.search(new).start() < MUTATION.search(new).start()

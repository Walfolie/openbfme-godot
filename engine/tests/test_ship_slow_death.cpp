// OpenBFME unit tests. GPL-3.0.
// Fork lane SHIPDEATH-1 (package B of docs/fork/MODULE_CENSUS.md) on synthetic data: ShipSlowDeathBehavior (CombatModules.h): the rates drawn when the
// slow death begins (pi / DestructionDelay * 0.5, each negated on a GameLogicRandomValue(0, 1) of 1, RW 0x861AA9) and the ship keeling over in its
// updates (RW 0x861B45) while it sinks, until it is destroyed.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Module/CombatModules.h"
#include "GameLogic/SimMath.h"

#include <cmath>
#include <vector>

using namespace combattest;

namespace
{
const char *const kShip =
	"Object ProbeShip\n"
	"  KindOf = SELECTABLE SHIP\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = ShipSlowDeathBehavior ModuleTag_Death\n"
	"    DeathTypes = ALL\n"
	"    SinkDelay = 0\n"
	"    SinkRate = 12.0\n"
	"    DestructionDelay = 10000\n"
	"  End\n"
	"End\n";

const ShipSlowDeathBehavior *shipDeath(const Object *o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		if (const ShipSlowDeathBehavior *sd = dynamic_cast<const ShipSlowDeathBehavior *>(m.get()))
		{
			return sd;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("shipdeath1: a dying ship draws its two keel rates and keels over while it sinks, its position kept in x / y, then it is destroyed (RW 0x861AA9, 0x861B45)")
{
	CombatWorld w(kShip);
	Object *ship = w.unit("ProbeShip", 'A', 300, 300);
	const ShipSlowDeathBehavior *sd = shipDeath(ship);
	REQUIRE(sd);
	CHECK(sd->rollRate() == 0.0f);
	w.logic->random().clearCallLog();
	w.logic->random().enableCallLog(true);
	DamageInfo d;
	d.m_input.m_amount = 1000.0f;
	d.m_input.m_damageType = DAMAGE_SLASH;
	ship->attemptDamage(d);
	w.logic->random().enableCallLog(false);
	REQUIRE(sd->isSlowDeathActivated());
	std::vector<int> signs;
	for (const GameLogicRandom::Call &c : w.logic->random().callLog())
	{
		if (c.file == "ShipSlowDeathBehavior.cpp")
		{
			CHECK(c.lo == 0);
			CHECK(c.hi == 1);
			signs.push_back(c.result);
		}
	}
	REQUIRE(signs.size() == 2u); // lines 0x40 then 0x46
	// DestructionDelay 10000 ms = 50 logic frames (5 per second): pi / 50 * 0.5 per update
	const float rate = SimMath::mulf32(SimMath::divf32(3.14159274f, 50.0f), 0.5f);
	CHECK(sd->rollRate() == (signs[0] == 1 ? -rate : rate));
	CHECK(sd->pitchRate() == (signs[1] == 1 ? -rate : rate));
	const float x0 = ship->getPosition()->x, y0 = ship->getPosition()->y;
	w.frames(25);
	REQUIRE(w.logic->findObjectByID(ship->getID()) == ship);
	// half the destruction delay: each axis turned by about a quarter turn, so the Z axis leans by about acos(cos(pi / 4)^2) = 60 degrees
	const float zz = ship->getBasis()[8];
	CHECK(zz > 0.4f); // tolerance: the updates the sink sleeps through are SlowDeathBehavior's (RW 0x860B39), only the size is checked
	CHECK(zz < 0.6f); // tolerance: as above
	// the basis stays a rotation (row lengths 1)
	for (int r = 0; r < 3; ++r)
	{
		const float *b = ship->getBasis() + 3 * r;
		CHECK(std::fabs(b[0] * b[0] + b[1] * b[1] + b[2] * b[2] - 1.0f) < 1e-4f); // tolerance: float rounding over 25 products
	}
	CHECK(ship->getPosition()->x == x0);
	CHECK(ship->getPosition()->y == y0);
	const ObjectID id = ship->getID();
	w.frames(40);
	CHECK(w.logic->findObjectByID(id) == nullptr); // destroyed after DestructionDelay
}

// OpenBFME retail tests for fork lane SPAWNUNIT-1: SpawnUnitBehavior's update (GameLogic/Module/SpawnUnitBehavior.h, RotWK RW 0x8BC9B0) on the Elven
// citadel: the eagle first in a queue of two or more is cancelled (and refunded), a lone eagle stays, the update sleeps 5 frames. They run only when
// ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP).
#include "doctest.h"
#include "StructureArena.h"

#include "Common/Money.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpawnUnitBehavior.h"

using namespace structtest;

namespace
{
SpawnUnitBehavior *spawnOf(Object &o)
{
	return dynamic_cast<SpawnUnitBehavior *>(o.findModule("SpawnUnitBehavior"));
}
} // namespace

TEST_CASE("spawnunit1 retail: the Elven citadel cancels a first queued eagle when more follow (RW 0x8BCA47 .. 0x8BCA9D); a lone entry is left")
{
	if (!hudtest::haveWorld("spawnunit1 retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Arena ar(s, "FactionElves", "FactionMordor");
	Object *citadel = ar.place("ElvenCitadel", 0, 400.0f, 600.0f);
	REQUIRE(citadel);
	SpawnUnitBehavior *spawn = spawnOf(*citadel);
	REQUIRE(spawn);
	CHECK(spawn->getUnitName() == "ElvenFortressEagle");
	CHECK_FALSE(spawn->maySpawn()); // SpawnOnce no
	ar.player(0)->getMoney()->deposit(1000000, false);
	ar.logic.productionSettings() = s.world->productionSettings();
	ar.player(0)->commandPoints().setFromScript(100000, 100000);
	for (int i = 0; i < 6; ++i) // past frame 5 (RW 0x8BC9E7)
	{
		ar.logic.runLogicFrame();
	}
	ProductionUpdateInterface *pu = citadel->getProductionUpdate();
	REQUIRE(pu);
	const ThingTemplate *eagle = s.world->things().findTemplate("ElvenFortressEagle");
	REQUIRE(eagle);
	// a lone eagle: one entry, nothing (RW 0x8BCA3E)
	REQUIRE(pu->queueCreateUnit(eagle, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	for (int i = 0; i < 6; ++i) // one 5-frame sleep at most
	{
		ar.logic.runLogicFrame();
	}
	CHECK(spawn->cancels() == 0u);
	pu->cancelAllProduction();
	CHECK(pu->getProductionCount() == 0);
	// two eagles: the next update cancels the head (refunded), the second stays; then one entry: nothing more
	const int before = (int)ar.player(0)->getMoney()->countMoney();
	REQUIRE(pu->queueCreateUnit(eagle, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	REQUIRE(pu->queueCreateUnit(eagle, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	REQUIRE(pu->getProductionCount() == 2);
	const int paid = (int)ar.player(0)->getMoney()->countMoney();
	for (int i = 0; i < 6; ++i)
	{
		ar.logic.runLogicFrame();
	}
	CHECK(spawn->cancels() == 1u);
	CHECK(pu->getProductionCount() == 1);
	CHECK((int)ar.player(0)->getMoney()->countMoney() - paid == (before - paid) / 2); // one eagle's cost came back (RW 0x8A13EE)
	for (int i = 0; i < 6; ++i)
	{
		ar.logic.runLogicFrame();
	}
	CHECK(spawn->cancels() == 1u);
}

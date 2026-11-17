// OpenBFME unit tests. GPL-3.0.
// Fork lane SENSE-1 (package B of docs/fork/MODULE_CENSUS.md) on synthetic data: SpecialEnemySenseUpdate (RW 0x89D7E6), Frodo's Sting: model
// condition SPECIAL_ENEMY_NEAR while the closest live enemy the SpecialEnemyFilter accepts is within ScanRange, cleared when there is none, the scan
// every ScanInterval.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialEnemySenseUpdate.h"
#include "GameLogic/Object/PartitionManager.h"

using namespace combattest;

namespace
{
const char *const kSense =
	"Object ProbeHobbit\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = SpecialEnemySenseUpdate ModuleTag_StingSeesOrcs\n"
	"    SpecialEnemyFilter = ANY +ORC +ProbeSpider\n"
	"    ScanRange = 200\n"
	"    ScanInterval = 2000\n"
	"  End\n"
	"End\n"
	"Object ProbeOrc\n"
	"  KindOf = SELECTABLE INFANTRY ORC\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object ProbeMan\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object ProbeSpider\n"
	"  KindOf = SELECTABLE MONSTER\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("sense1: Sting glows while a live enemy orc (or the named template) is within ScanRange, not for an ally, another kind or one out of range (RW 0x89D7E6)")
{
	CombatWorld w(kSense);
	w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f); // a world without a map searches no range (PartitionManager.h)
	const int bit = AIUpdateInterface::modelConditionBit("SPECIAL_ENEMY_NEAR");
	REQUIRE(bit == 258);
	Object *frodo = w.unit("ProbeHobbit", 'A', 300, 300);
	CHECK_FALSE(frodo->testModelCondition(bit));
	// ScanInterval 2000 ms = 10 logic frames: every case below waits one full interval
	w.unit("ProbeOrc", 'A', 350, 300);   // an ally orc
	w.unit("ProbeMan", 'B', 360, 300);   // an enemy the filter rejects
	w.unit("ProbeOrc", 'B', 520, 300);   // an enemy orc 220 away
	w.frames(11);
	CHECK_FALSE(frodo->testModelCondition(bit));
	Object *orc = w.unit("ProbeOrc", 'B', 450, 300); // 150 away
	w.frames(11);
	CHECK(frodo->testModelCondition(bit));
	orc->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	w.frames(11);
	CHECK_FALSE(frodo->testModelCondition(bit)); // the dead one is not seen, the far one is still out of range
	Object *spider = w.unit("ProbeSpider", 'B', 300, 450); // the template named in the filter
	w.frames(11);
	CHECK(frodo->testModelCondition(bit));
	spider->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	w.frames(11);
	CHECK_FALSE(frodo->testModelCondition(bit));
}

// OpenBFME unit tests. GPL-3.0.
// Fork lane AIM-1: AimWeaponBehavior (GameLogic/Module/AimWeaponBehavior.h, RotWK RW 0x85E1C7 .. 0x85E50E) on synthetic data: the field table and its
// defaults, the pitch condition from the slope to the target record, AIM_NEAR / AIM_FAR from the distance, the five conditions cleared when the attack ends.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/AimWeaponBehavior.h"
#include "GameLogic/Object/Object.h"

#include <cfloat>
#include <string>

using namespace combattest;

namespace
{
const char kBow[] =
	"Weapon AimTestBow\n"
	"  AttackRange = 200\n"
	"  IsAimingWeapon = Yes\n"
	"  DelayBetweenShots = 800\n"
	"  PreAttackDelay = 400\n"
	"  PreAttackType = PER_SHOT\n"
	"  DamageNugget\n"
	"    Damage = 1\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n";

std::string archer(const std::string &name, const std::string &aimFields)
{
	return "Object " + name + "\n"
		   "  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
		   "  Geometry = CYLINDER\n"
		   "  GeometryMajorRadius = 8\n"
		   "  GeometryHeight = 20\n"
		   "  ArmorSet\n"
		   "    Conditions = None\n"
		   "    Armor = PlainArmor\n"
		   "  End\n"
		   "  WeaponSet\n"
		   "    Conditions = None\n"
		   "    Weapon = PRIMARY AimTestBow\n"
		   "  End\n"
		   "  Body = ActiveBody ModuleTag_Body\n"
		   "    MaxHealth = 60\n"
		   "  End\n"
		   "  Behavior = AIUpdateInterface ModuleTag_AI\n"
		   "  End\n"
		   "  Behavior = AimWeaponBehavior ModuleTag_Aim\n" +
		aimFields +
		"  End\n"
		"  LocomotorSet\n"
		"    Locomotor = WalkerLoco\n"
		"    Condition = SET_NORMAL\n"
		"    Speed = 55\n"
		"  End\n"
		"End\n";
}

AimWeaponBehavior *aimOf(Object &o)
{
	return dynamic_cast<AimWeaponBehavior *>(o.findModule("AimWeaponBehavior"));
}

int mc(const char *name)
{
	return CombatNames::modelCondition(name);
}
} // namespace

TEST_CASE("aim1: the field table (RW 0xC58178) and the defaults of RW 0x85E251")
{
	const std::string ini = std::string(kBow) + archer("AimDefaults", "") + archer("AimSet", "    AimLowThreshold = -0.25\n    AimHighThreshold = 0.5\n    AimNearDistance = 40\n    AimFarDistance = 120\n");
	CombatWorld w(ini.c_str());
	Object *d = w.unit("AimDefaults", 'A', 100, 100);
	Object *s = w.unit("AimSet", 'A', 200, 100);
	w.frames(1);
	REQUIRE(aimOf(*d));
	REQUIRE(aimOf(*s));
	const auto &dd = *dynamic_cast<const AimWeaponBehaviorModuleData *>(aimOf(*d)->getModuleData());
	const auto &sd = *dynamic_cast<const AimWeaponBehaviorModuleData *>(aimOf(*s)->getModuleData());
	CHECK(dd.m_aimLowThreshold == -0.05f);
	CHECK(dd.m_aimHighThreshold == 0.05f);
	CHECK(dd.m_aimNearDistance == 0.0f);
	CHECK(dd.m_aimFarDistance == FLT_MAX);
	CHECK(sd.m_aimLowThreshold == -0.25f);
	CHECK(sd.m_aimHighThreshold == 0.5f);
	CHECK(sd.m_aimNearDistance == 40.0f);
	CHECK(sd.m_aimFarDistance == 120.0f);
}

TEST_CASE("aim1: an attacking archer on flat ground aims STRAIGHT and NEAR / FAR by distance; the conditions clear once it stops (RW 0x85E30B)")
{
	// thresholds that turn the flat slope 0 into each pitch: HIGH when 0 > high, LOW when low > 0
	const std::string ini = std::string(kBow) + archer("AimFlat", "    AimNearDistance = 40\n    AimFarDistance = 120\n") +
		archer("AimHighShot", "    AimHighThreshold = -0.5\n    AimLowThreshold = -1\n") + archer("AimLowShot", "    AimHighThreshold = 1\n    AimLowThreshold = 0.5\n");
	CombatWorld w(ini.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *nearShooter = w.unit("AimFlat", 'A', 100, 100);
	Object *farShooter = w.unit("AimFlat", 'A', 100, 400);
	Object *high = w.unit("AimHighShot", 'A', 100, 600);
	Object *low = w.unit("AimLowShot", 'A', 100, 800);
	Object *v1 = w.unit("Dummy", 'B', 130, 100);  // 30 away: below AimNearDistance
	Object *v2 = w.unit("Dummy", 'B', 250, 400);  // 150 away: beyond AimFarDistance
	Object *v3 = w.unit("Dummy", 'B', 150, 600);
	Object *v4 = w.unit("Dummy", 'B', 150, 800);
	w.frames(1);
	CHECK_FALSE(aimOf(*nearShooter)->aimSet());
	REQUIRE(nearShooter->getAIUpdateInterface()->aiAttackObject(v1, CMD_FROM_PLAYER));
	REQUIRE(farShooter->getAIUpdateInterface()->aiAttackObject(v2, CMD_FROM_PLAYER));
	REQUIRE(high->getAIUpdateInterface()->aiAttackObject(v3, CMD_FROM_PLAYER));
	REQUIRE(low->getAIUpdateInterface()->aiAttackObject(v4, CMD_FROM_PLAYER));
	// the target record (+ 0x3A8) is written by the pre-fire (RW 0x69213E): until then it is the origin, as in retail; wait for it, then one more update
	auto recorded = [](Object *o, Object *v) { return o->getWeapons()->drawTargetID() == v->getID(); };
	const int n = w.runUntil([&] { return recorded(nearShooter, v1) && recorded(farShooter, v2) && recorded(high, v3) && recorded(low, v4); }, 40);
	REQUIRE(n < 40); // tolerance: a frame budget, not a result: the pre-fire frame depends on the approach and the PreAttackDelay
	w.frames(1);
	CHECK(aimOf(*nearShooter)->aimSet());
	CHECK(aimOf(*low)->aimSet());
	CHECK(nearShooter->testModelCondition(mc("AIM_STRAIGHT")));
	CHECK(nearShooter->testModelCondition(mc("AIM_NEAR")));
	CHECK_FALSE(nearShooter->testModelCondition(mc("AIM_FAR")));
	CHECK(farShooter->testModelCondition(mc("AIM_STRAIGHT")));
	CHECK(farShooter->testModelCondition(mc("AIM_FAR")));
	CHECK_FALSE(farShooter->testModelCondition(mc("AIM_NEAR")));
	CHECK(high->testModelCondition(mc("AIM_HIGH")));
	CHECK_FALSE(high->testModelCondition(mc("AIM_STRAIGHT")));
	CHECK_FALSE(high->testModelCondition(mc("AIM_NEAR"))); // the defaults: 0 near, FLT_MAX far: neither
	CHECK_FALSE(high->testModelCondition(mc("AIM_FAR")));
	CHECK(low->testModelCondition(mc("AIM_LOW")));
	// the attack ends: the next update clears all five
	nearShooter->getAIUpdateInterface()->aiIdle(CMD_FROM_PLAYER);
	w.runUntil([&] { return !aimOf(*nearShooter)->aimSet(); }, 20);
	CHECK_FALSE(aimOf(*nearShooter)->aimSet());
	for (const char *name : { "AIM_HIGH", "AIM_STRAIGHT", "AIM_LOW", "AIM_NEAR", "AIM_FAR" })
	{
		CHECK_FALSE(nearShooter->testModelCondition(mc(name)));
	}
}

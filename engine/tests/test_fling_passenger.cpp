// OpenBFME unit tests. GPL-3.0.
// Fork lane FLING-1: FlingPassengerSpecialAbilityUpdate (GameLogic/Module/FlingPassengerSpecialAbilityUpdate.h, RotWK RW 0x898672 .. 0x8987FB) on synthetic
// data: the field table, the first rider taken out of the contain, the landing warhead fired at it (a dead rider DYING + SPLATTED), a living rider left alive.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/FlingPassengerSpecialAbilityUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Weapon.h"

#include <string>

namespace
{
const char kPowers[] = R"ini(
SpecialPower FlingTestPower
 Enum = SPECIAL_GENERAL_TARGETLESS
 ReloadTime = 0
End
)ini";

const char kWeapon[] = R"ini(
Weapon FlingTestWarhead
 AttackRange = 10
 DamageNugget
  Damage = 1000
  Radius = 0.0
  DelayTime = 0
  DamageType = CRUSH
  DeathType = NORMAL
 End
End
)ini";

std::string eater(const std::string &name, const std::string &flingFields)
{
	return "Object " + name + R"ini(
 KindOf = MONSTER SELECTABLE
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 30
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 500
 End
 Behavior = AIUpdateInterface ModuleTag_AI
 End
 Behavior = FlingPassengerSpecialAbilityUpdate ModuleTag_Fling
  SpecialPowerTemplate = FlingTestPower
  UnpackTime = 1250
)ini" + flingFields + R"ini( End
 Behavior = TransportContain ModuleTag_Contain
  ObjectStatusOfContained = UNSELECTABLE
  PassengerFilter = ANY +INFANTRY
  Slots = 1
  AllowEnemiesInside = Yes
 End
End
)ini";
}

struct FlingWorld
{
	combattest::CombatWorld w;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	FlingWorld()
	{
		TheSpecialPowerStore = &powers;
		w.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		std::string err = w.w.load(kPowers, INI_LOAD_OVERWRITE, "flingpowers.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.w.load(kWeapon, INI_LOAD_OVERWRITE, "flingweapon.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.w.load(eater("FlingEater", "  FlingPassengerVelocity = X:1 Y:2 Z:3\n  FlingPassengerLandingWarhead = FlingTestWarhead\n") +
				eater("FlingDropper", "") + eater("FlingUnknown", "  FlingPassengerLandingWarhead = NoSuchWarhead\n"),
			INI_LOAD_OVERWRITE, "flingers.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		w.combat().setAutoAcquireEnabled(false);
	}
	~FlingWorld() { TheSpecialPowerStore = savedPowers; }
};

FlingPassengerSpecialAbilityUpdate *flingOf(Object &o)
{
	for (const auto &m : o.modules())
	{
		if (auto *f = dynamic_cast<FlingPassengerSpecialAbilityUpdate *>(m.get()))
		{
			return f;
		}
	}
	return nullptr;
}

bool inside(const Object &holder, const Object &rider)
{
	const ContainModuleInterface *c = holder.getContain();
	if (!c)
	{
		return false;
	}
	for (const Object *o : *c->getContainedItemsList())
	{
		if (o == &rider)
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("fling1: the field table (RW 0xC0556C after SpecialAbilityUpdate's): the velocity is parsed, the warhead resolved, an unknown one null")
{
	FlingWorld f;
	Object *eaterObj = f.w.unit("FlingEater", 'A', 200.0f, 200.0f);
	Object *dropper = f.w.unit("FlingDropper", 'A', 300.0f, 200.0f);
	Object *unknown = f.w.unit("FlingUnknown", 'A', 400.0f, 200.0f);
	f.w.frames(1);
	FlingPassengerSpecialAbilityUpdate *a = flingOf(*eaterObj);
	FlingPassengerSpecialAbilityUpdate *b = flingOf(*dropper);
	FlingPassengerSpecialAbilityUpdate *c = flingOf(*unknown);
	REQUIRE(a);
	REQUIRE(b);
	REQUIRE(c);
	CHECK(a->flingData()->m_flingPassengerVelocity.x == 1.0f);
	CHECK(a->flingData()->m_flingPassengerVelocity.y == 2.0f);
	CHECK(a->flingData()->m_flingPassengerVelocity.z == 3.0f);
	REQUIRE(a->flingData()->m_flingPassengerLandingWarhead);
	CHECK(a->flingData()->m_flingPassengerLandingWarhead->getName() == "FlingTestWarhead");
	CHECK(a->flingData()->m_unpackFrames == 7u); // the base table parses first: 1250 ms
	CHECK(b->flingData()->m_flingPassengerLandingWarhead == nullptr);
	CHECK(c->flingData()->m_flingPassengerLandingWarhead == nullptr);
}

TEST_CASE("fling1: the trigger (RW 0x89875A) takes the first rider out; the warhead kills it (DYING, SPLATTED); without one it lives, unmarked")
{
	FlingWorld f;
	Object *eaterObj = f.w.unit("FlingEater", 'A', 200.0f, 200.0f);
	Object *dropper = f.w.unit("FlingDropper", 'A', 400.0f, 200.0f);
	Object *victim = f.w.unit("Dummy", 'B', 220.0f, 200.0f);
	Object *tree = f.w.unit("Dummy", 'A', 420.0f, 200.0f);
	f.w.frames(1);
	REQUIRE(eaterObj->getContain());
	REQUIRE(dropper->getContain());
	REQUIRE(eaterObj->getContain()->addToContain(victim));
	REQUIRE(dropper->getContain()->addToContain(tree));
	CHECK(inside(*eaterObj, *victim));
	CHECK(inside(*dropper, *tree));
	const int dying = CombatNames::modelCondition("DYING");
	const int splatted = CombatNames::modelCondition("SPLATTED");
	REQUIRE(dying == 62);
	REQUIRE(splatted == 122);

	// an empty contain: nothing (the base trigger only)
	FlingPassengerSpecialAbilityUpdate *empty = flingOf(*f.w.unit("FlingEater", 'A', 600.0f, 200.0f));
	REQUIRE(empty);
	empty->triggerForTest();

	flingOf(*eaterObj)->triggerForTest();
	CHECK_FALSE(inside(*eaterObj, *victim));
	CHECK(victim->isEffectivelyDead());
	CHECK(victim->testModelCondition(dying));
	CHECK(victim->testModelCondition(splatted));

	flingOf(*dropper)->triggerForTest();
	CHECK_FALSE(inside(*dropper, *tree));
	CHECK_FALSE(tree->isEffectivelyDead());
	CHECK_FALSE(tree->testModelCondition(dying));
	CHECK_FALSE(tree->testModelCondition(splatted));
}

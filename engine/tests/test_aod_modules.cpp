// OpenBFME unit tests. GPL-3.0.
// Fork lane AOD-1: AODCrushCollide and OathbreakersFadeAwayBehavior (GameLogic/Module/OathbreakerModules.h, RotWK RW 0x8C0201 / 0x8C042F, RW 0x8BCC82)
// on synthetic data: the field tables, an enemy touched takes the normal hit and the attack look lasts 2 seconds, an ally is left alone, a SpecialObject
// takes the special hit and hits back with the self row, OATH_FULLFILLED stops the hits and fades the object out over FadeOutTime, then destroys it.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/OathbreakerModules.h"
#include "GameLogic/Object/Object.h"

using namespace combattest;

namespace
{
const char *const kGhosts =
	"Object GhostProbe\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  Behavior = AODCrushCollide ModuleTag_Crush\n"
	"    SmallFXList = FX_AODHitSmall\n"
	"    Damage = 30\n"
	"    DamageType = MAGIC\n"
	"    DeathType = SUPERNATURAL\n"
	"    SpecialObject = NONE +SpecialDummy\n"
	"    SpecialDamage = 5.0\n"
	"    SpecialDamageType = MAGIC\n"
	"    SpecialDeathType = CRUSHED\n"
	"    SelfDamage = 40.0\n"
	"    SelfDamageType = FLAME\n"
	"    SelfDeathType = BURNED\n"
	"  End\n"
	"  Behavior = OathbreakersFadeAwayBehavior ModuleTag_Fade\n"
	"    FadeOutTime = 2000\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object SpecialDummy\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"End\n";

AODCrushCollide *crushOf(Object &o)
{
	return dynamic_cast<AODCrushCollide *>(o.findModule("AODCrushCollide"));
}
OathbreakersFadeAwayBehavior *fadeOf(Object &o)
{
	return dynamic_cast<OathbreakersFadeAwayBehavior *>(o.findModule("OathbreakersFadeAwayBehavior"));
}
} // namespace

TEST_CASE("aod1: the field tables (RW 0xC710A8, RW 0xC6FF84)")
{
	CombatWorld w(kGhosts);
	Object *g = w.unit("GhostProbe", 'A', 300, 300);
	REQUIRE(crushOf(*g));
	REQUIRE(fadeOf(*g));
	const auto &d = *dynamic_cast<const AODCrushCollideModuleData *>(crushOf(*g)->getModuleData());
	CHECK(d.m_fx[0] == "FX_AODHitSmall");
	CHECK(d.m_fx[1].empty());
	CHECK(d.m_damage == 30.0f);
	CHECK(d.m_specialDamage == 5.0f);
	CHECK(d.m_selfDamage == 40.0f);
	CHECK(d.m_damageType != d.m_selfDamageType);
	const auto &f = *dynamic_cast<const OathbreakersFadeAwayBehaviorModuleData *>(fadeOf(*g)->getModuleData());
	CHECK(f.m_fadeOutTime == 10u); // 2000 ms at 5 logic frames a second
	CHECK(AODCrushCollide::deadCollideSize(*g) == 0); // no DeadCollideSize: SMALL (RW 0x73FFAF)
}

TEST_CASE("aod1: an enemy touched takes the normal hit; the attack look lasts 2 seconds; an ally is left alone (RW 0x8C0201, 0x8C042F)")
{
	CombatWorld w(kGhosts);
	w.combat().setAutoAcquireEnabled(false);
	Object *g = w.unit("GhostProbe", 'A', 300, 300);
	Object *ally = w.unit("Dummy", 'A', 300, 340);
	w.frames(2);
	AODCrushCollide *crush = crushOf(*g);
	REQUIRE(crush);
	crush->onCollide(ally, nullptr, nullptr);
	CHECK_FALSE(crush->active());
	CHECK(w.health(ally) == 100.0f);
	Object *enemy = w.unit("Dummy", 'B', 340, 300);
	w.frames(1);
	const UnsignedInt now = w.logic->getFrame();
	crush->onCollide(enemy, nullptr, nullptr);
	CHECK(crush->active());
	CHECK(crush->lastVictim() == enemy->getID());
	CHECK(crush->attackLookEnd() == now + 10u);
	static const int kFiringA = CombatNames::modelCondition("FIRING_A");
	CHECK(g->testModelCondition(kFiringA));
	CHECK(w.health(enemy) == 70.0f); // 30 MAGIC, PlainArmor takes it in full
	// the look ends once its frame has passed
	w.frames(10);
	CHECK(g->testModelCondition(kFiringA));
	w.frames(2);
	CHECK_FALSE(g->testModelCondition(kFiringA));
	CHECK_FALSE(crush->active());
}

TEST_CASE("aod1: a SpecialObject takes the special hit and hits back with the self row (RW 0x8C0349 .. 0x8C03D5)")
{
	CombatWorld w(kGhosts);
	w.combat().setAutoAcquireEnabled(false);
	Object *g = w.unit("GhostProbe", 'A', 300, 300);
	Object *special = w.unit("SpecialDummy", 'B', 340, 300);
	Object *plain = w.unit("Dummy", 'B', 300, 340);
	w.frames(2);
	AODCrushCollide *crush = crushOf(*g);
	REQUIRE(crush);
	crush->onCollide(special, nullptr, nullptr);
	crush->onCollide(plain, nullptr, nullptr);
	w.frames(1);
	const float specialLoss = 100.0f - w.health(special), plainLoss = 100.0f - w.health(plain);
	CHECK(specialLoss == 5.0f);
	CHECK(plainLoss == 30.0f);
	CHECK(w.health(g) == 60.0f); // the self row: 40 FLAME
}

TEST_CASE("aod1: OATH_FULLFILLED stops the hits and fades the object out over FadeOutTime, then it is destroyed (RW 0x8BCC82)")
{
	CombatWorld w(kGhosts);
	w.combat().setAutoAcquireEnabled(false);
	Object *g = w.unit("GhostProbe", 'A', 300, 300);
	Object *enemy = w.unit("Dummy", 'B', 340, 300);
	w.frames(2);
	const ObjectID id = g->getID();
	static const int kOath = CombatNames::modelCondition("OATH_FULLFILLED");
	g->setModelConditionState(kOath, true);
	crushOf(*g)->onCollide(enemy, nullptr, nullptr);
	CHECK(w.health(enemy) == 100.0f);
	CHECK_FALSE(crushOf(*g)->active());
	OathbreakersFadeAwayBehavior *fade = fadeOf(*g);
	REQUIRE(fade);
	CHECK(g->logicDrawableOpacity() == -1.0f); // none set before the fade
	w.frames(6); // the module wakes within a second (LOGICFRAMES_PER_SECOND) and then runs every frame
	CHECK(fade->started());
	CHECK_FALSE(g->isScriptSelectable());
	const float step = 1.0f / 10.0f;
	CHECK(fade->opacity() == ((1.0f - step) - step) - step); // three fading frames, float32 each (RW 0x8BCCCE .. 0x8BCCD7)
	CHECK(g->logicDrawableOpacity() == fade->opacity());
	w.frames(12); // 10 frames of 0.1 each
	CHECK(w.logic->findObjectByID(id) == nullptr);
}

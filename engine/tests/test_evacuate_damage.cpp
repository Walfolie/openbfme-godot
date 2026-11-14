// OpenBFME unit tests. GPL-3.0.
// Fork lane EVACUATE-1: EvacuateDamage (GameLogic/Module/EvacuateDamage.h, RotWK RW 0x8BEDF8 .. 0x8BF22E) on synthetic data: the field table, the tracked
// hits and their time span, the panic threshold (the records cleared, the unported flee reported), the evacuation weapon that is never tracked.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/EvacuateDamage.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <string>

using namespace combattest;

namespace
{
const char *const kProbe =
	"Object EvacProbe\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 30\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = EvacuateDamage ModuleTag_Evac\n"
	"    WeaponThatCausesEvacuation = BowWeapon\n"
	"    DamageTypeToTrack = SLASH\n"
	"    DamageToPanicThreshold = 30%\n"
	"    TrackingTimeSpan = 1000\n" // 5 frames
	"  End\n"
	"End\n"
	"Object EvacDefaults\n"
	"  KindOf = STRUCTURE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = EvacuateDamage ModuleTag_Evac\n"
	"    WeaponThatCausesEvacuation = NoSuchWeapon\n"
	"  End\n"
	"End\n";

EvacuateDamage *evacOf(Object &o)
{
	return dynamic_cast<EvacuateDamage *>(o.findModule("EvacuateDamage"));
}

void hit(Object &victim, const Object &source, float amount, int type)
{
	DamageInfo d;
	d.m_input.m_sourceID = source.getID();
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	victim.attemptDamage(d);
}

bool stopNoted(GameLogic &logic)
{
	const std::vector<std::string> stops = logic.report().stops;
	return std::any_of(stops.begin(), stops.end(), [](const std::string &s) { return s == EvacuateDamage::stopLine(); });
}
} // namespace

TEST_CASE("evacuate1: the field table's defaults and an evacuation weapon TheWeaponStore does not know (RW 0x8BEE59 / 0x8BEDF8)")
{
	CombatWorld w(kProbe);
	Object *d = w.unit("EvacDefaults", 'A', 300, 300);
	w.frames(1);
	EvacuateDamage *e = evacOf(*d);
	REQUIRE(e);
	const EvacuateDamageModuleData *data = dynamic_cast<const EvacuateDamageModuleData *>(e->getModuleData());
	REQUIRE(data);
	CHECK(data->m_weaponThatCausesEvacuation.empty()); // an unknown name is not kept
	CHECK(data->m_damageTypeToTrack == DAMAGE_PIERCE);   // 3
	CHECK(data->m_damageToPanicThreshold == 0.1f);
	CHECK(data->m_trackingTimeSpan == 6u);
}

TEST_CASE("evacuate1: hits of the tracked type are summed over TrackingTimeSpan; at the threshold the records clear and the unported flee is reported")
{
	CombatWorld w(kProbe);
	Object *probe = w.unit("EvacProbe", 'A', 300, 300);
	Object *sword = w.unit("Swordsman", 'B', 330, 300); // SwordWeapon (SLASH)
	Object *bow = w.unit("Archer", 'B', 360, 300);      // BowWeapon: the evacuation weapon
	w.frames(2);
	EvacuateDamage *e = evacOf(*probe);
	REQUIRE(e);
	const auto &data = *dynamic_cast<const EvacuateDamageModuleData *>(e->getModuleData());
	CHECK(data.m_weaponThatCausesEvacuation == "BowWeapon");
	CHECK(data.m_trackingTimeSpan == 5u); // 1000 ms
	// another type is not tracked
	hit(*probe, *sword, 10.0f, DAMAGE_PIERCE);
	CHECK(e->records().empty());
	// the evacuation weapon's hit evacuates (no contain here: nothing) and is never tracked, whatever its type
	hit(*probe, *bow, 10.0f, DAMAGE_SLASH);
	CHECK(e->records().empty());
	// SLASH from the sword: tracked with its input amount
	hit(*probe, *sword, 10.0f, DAMAGE_SLASH);
	REQUIRE(e->records().size() == 1);
	CHECK(e->records()[0].second == 10.0f);
	CHECK(e->recentDamage() == 10.0f);
	// a record at or before now - TrackingTimeSpan is erased
	w.frames(5);
	CHECK(e->recentDamage() == 0.0f);
	CHECK(e->records().empty());
	// three hits of 10 within the span reach 30% of 100: the panic clears the records (the object has an AI) and reports the flee it cannot make
	CHECK_FALSE(stopNoted(*w.logic));
	hit(*probe, *sword, 10.0f, DAMAGE_SLASH);
	hit(*probe, *sword, 10.0f, DAMAGE_SLASH);
	CHECK(e->records().size() == 2);
	CHECK_FALSE(stopNoted(*w.logic));
	hit(*probe, *sword, 10.0f, DAMAGE_SLASH);
	CHECK(e->records().empty());
	CHECK(stopNoted(*w.logic));
}

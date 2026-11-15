// OpenBFME unit tests. GPL-3.0.
// Fork lane DAMAGEFIELD-1: DamageFieldUpdate (GameLogic/Module/DamageFieldUpdate.h, RotWK RW 0x894773 .. 0x894C3E) on synthetic data: the field table
// after FireWeaponUpdate's, one shot at each enemy that enters the radius, none while it stays, its id kept until it leaves, allies ignored.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/DamageFieldUpdate.h"
#include "GameLogic/Object/Object.h"

using namespace combattest;

namespace
{
const char *const kField =
	"Object FieldProbe\n"
	"  KindOf = STRUCTURE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = DamageFieldUpdate ModuleTag_Field\n"
	"    Radius = 50\n"
	"    ObjectFilter = ALL ENEMIES\n"
	"    FireWeaponNugget\n"
	"      WeaponName = SwordWeapon\n"
	"      FireDelay = 0\n"
	"      OneShot = No\n"
	"    End\n"
	"  End\n"
	"End\n";

DamageFieldUpdate *fieldOf(Object &o)
{
	return dynamic_cast<DamageFieldUpdate *>(o.findModule("DamageFieldUpdate"));
}
} // namespace

TEST_CASE("damagefield1: the field table (RW 0xC64018 after RW 0xC62A40)")
{
	CombatWorld w(kField);
	Object *f = w.unit("FieldProbe", 'A', 300, 300);
	REQUIRE(fieldOf(*f));
	const auto &d = *dynamic_cast<const DamageFieldUpdateModuleData *>(fieldOf(*f)->getModuleData());
	CHECK(d.m_radius == 50);
	CHECK(d.m_requiredUpgrade.empty());
	REQUIRE(d.m_nuggets.size() == 1u);
	CHECK(d.m_nuggets[0].weaponName == "SwordWeapon");
}

TEST_CASE("damagefield1: an enemy entering the radius is shot once and kept until it leaves; an ally is never shot (RW 0x8949A2)")
{
	CombatWorld w(kField);
	w.combat().setAutoAcquireEnabled(false);
	Object *f = w.unit("FieldProbe", 'A', 300, 300);
	Object *ally = w.unit("Dummy", 'A', 320, 300);
	w.frames(3);
	DamageFieldUpdate *field = fieldOf(*f);
	REQUIRE(field);
	CHECK(field->inField().empty()); // the ally does not count
	CHECK(field->shots() == 0u);
	Object *enemy = w.unit("Dummy", 'B', 330, 300); // 30 away
	w.frames(3);
	CHECK(field->inField().count(enemy->getID()) == 1u);
	CHECK(field->shots() == 1u);
	w.frames(10); // the sword is ready again long before: no second shot while it stays
	CHECK(field->shots() == 1u);
	CHECK(w.health(enemy) == 95.0f); // SwordWeapon: 10 SLASH, PlainArmor takes 50%
	CHECK(w.health(ally) == 100.0f);
	// it leaves (teleported 200 away): the id goes; back inside: shot again
	Coord3D away{ 530.0f, 300.0f, enemy->getPosition()->z };
	enemy->setPosition(&away);
	w.frames(3);
	CHECK(field->inField().empty());
	Coord3D back{ 330.0f, 300.0f, enemy->getPosition()->z };
	enemy->setPosition(&back);
	w.frames(3);
	CHECK(field->shots() == 2u);
	CHECK(w.health(enemy) == 90.0f);
}

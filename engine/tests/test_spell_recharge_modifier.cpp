// OpenBFME unit tests. GPL-3.0.
// Fork lane SPELLRECHARGE-1: SpellRechargeModifierUpgrade (GameLogic/Module/SpellRechargeModifierUpgrade.h, RotWK RW 0x8B9F57 .. 0x8BA2B5) on synthetic data:
// the field table, the player's count (Player + 0x71C) and the recharge discount (Player + 0x718) it picks, the capture and the destruction.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpellRechargeModifierUpgrade.h"
#include "GameLogic/Object/Object.h"

using namespace combattest;

namespace
{
const char *const kFire =
	"Object TestSignalFire\n"
	"  KindOf = STRUCTURE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = SpellRechargeModifierUpgrade ModuleTag_Spell\n"
	"    LabelForPalantirString = GUI:SpellBookRefreshReduction\n"
	"    StartsActive = Yes\n"
	"    Percentage = -25%\n"
	"    Percentage = -50%\n"
	"  End\n"
	"End\n";

SpellRechargeModifierUpgrade *srmOf(Object &o)
{
	return dynamic_cast<SpellRechargeModifierUpgrade *>(o.findModule("SpellRechargeModifierUpgrade"));
}
} // namespace

TEST_CASE("spellrecharge1: the field table (RW 0xC6F0D0): the percentages in order, StartsActive, the label")
{
	CombatWorld w(kFire);
	Object *f = w.unit("TestSignalFire", 'A', 300, 300);
	REQUIRE(srmOf(*f));
	const auto &d = *dynamic_cast<const SpellRechargeModifierUpgradeModuleData *>(srmOf(*f)->getModuleData());
	REQUIRE(d.m_percentage.size() == 2u);
	CHECK(d.m_percentage[0] == doctest::Approx(-0.25f)); // tolerance: parsePercentToReal's x87 product (pinned by the INI tests)
	CHECK(d.m_percentage[1] == doctest::Approx(-0.5f));  // tolerance: as above
	CHECK(d.m_startsActive);
	CHECK(d.m_labelForPalantirString == "GUI:SpellBookRefreshReduction");
}

TEST_CASE("spellrecharge1: each completed fire counts for its owner; the discount is Percentage[count - 1] clamped; capture and destruction move the count")
{
	CombatWorld w(kFire);
	Player *alice = w.playerOf('A');
	Player *bob = w.playerOf('B');
	Object *probe = w.unit("TestSignalFire", 'B', 900, 900); // never completed: counts for nobody
	const std::vector<float> pct = dynamic_cast<const SpellRechargeModifierUpgradeModuleData *>(srmOf(*probe)->getModuleData())->m_percentage;
	REQUIRE(pct.size() == 2u);
	CHECK(alice->science().rechargeModifierCount() == 0);
	CHECK(alice->science().rechargeDiscount() == 0.0f);
	Object *f1 = w.unit("TestSignalFire", 'A', 300, 300);
	Object *f2 = w.unit("TestSignalFire", 'A', 400, 300);
	Object *f3 = w.unit("TestSignalFire", 'A', 500, 300);
	CHECK(alice->science().rechargeModifierCount() == 0); // not complete
	f1->friend_onBuildComplete(); // StartsActive (RW 0x8B9FB6)
	CHECK(alice->science().rechargeModifierCount() == 1);
	CHECK(alice->science().rechargeDiscount() == pct[0]);
	CHECK(srmOf(*f1)->isAlreadyUpgraded());
	f2->friend_onBuildComplete();
	CHECK(alice->science().rechargeModifierCount() == 2);
	CHECK(alice->science().rechargeDiscount() == pct[1]);
	f3->friend_onBuildComplete(); // three fires, two percentages: the last value
	CHECK(alice->science().rechargeModifierCount() == 3);
	CHECK(alice->science().rechargeDiscount() == pct[1]);
	// capture (RW 0x8BA05E): Alice's count - 1, Bob's + 1, each owner's discount recomputed
	f3->setTeam(w.teamB());
	CHECK(alice->science().rechargeModifierCount() == 2);
	CHECK(alice->science().rechargeDiscount() == pct[1]);
	CHECK(bob->science().rechargeModifierCount() == 1);
	CHECK(bob->science().rechargeDiscount() == pct[0]);
	// destruction (slot 8 -> the removal RW 0x8B9FE0, executed): the owner's count - 1
	w.logic->destroyObject(f2);
	w.logic->processDestroyList();
	CHECK(alice->science().rechargeModifierCount() == 1);
	CHECK(alice->science().rechargeDiscount() == pct[0]);
	w.logic->destroyObject(f1);
	w.logic->processDestroyList();
	CHECK(alice->science().rechargeModifierCount() == 0);
	CHECK(alice->science().rechargeDiscount() == pct[0]); // count 0 clamps to index 0: the discount stays (retail never clears it)
}

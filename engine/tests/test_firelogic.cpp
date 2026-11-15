// OpenBFME unit tests. GPL-3.0.
// Fork lane FIRELOGIC-1: TheFireLogicSystem (GameLogic/FireLogicSystem.h, RotWK RW 0x685E07 .. 0x688D1E) on synthetic data: the INI, the grid over the terrain's
// cell types, lighting a circle and an arc, the burn, the spread to the neighbours, the burn damage to the objects standing in burning cells, the fuel
// change, the objects' cells and the hash.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/FireLogicSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/WeaponNugget.h"

using namespace combattest;

namespace
{
// data/ini/firelogicsystem.ini's values (RotWK 2.01)
const char *const kFireIni =
	"FireLogicSystem\n"
	"  MaxCellsBurning = 1000\n"
	"  TerrainCellType 0\n"
	"    Color = R:0 G:0 B:0\n"
	"    Name = \"*Fire resistant\"\n"
	"  End\n"
	"  TerrainCellType 1\n"
	"    Color = R:0 G:255 B:0\n"
	"    Name = \"Grass\"\n"
	"    Fuel = 100\n"
	"    MaxBurnRate = 20\n"
	"    Decay = 4\n"
	"    Resistance = 10\n"
	"  End\n"
	"  TerrainCellType 2\n"
	"    Name = \"Highly flammable\"\n"
	"    Fuel = 500\n"
	"    MaxBurnRate = 100\n"
	"    Decay = 1\n"
	"    Resistance = 2\n"
	"  End\n"
	"End\n";

struct FireWorld
{
	CombatWorld w;
	FireLogicSystem &fire;

	explicit FireWorld(int type = 1)
		: w("", 100)
		, fire(w.logic->fireLogic())
	{
		FireLogicSettings s;
		std::string err;
		REQUIRE_MESSAGE(FireLogicSettings::loadText(kFireIni, s, &err), err);
		w.terrain.fireType = type;
		fire.configure(s, nullptr);
		fire.newMap(&w.terrain);
		fire.loadPostProcess();
	}
	int litCells() const
	{
		int n = 0;
		for (int x = 0; x < fire.columns(); ++x)
		{
			for (int y = 0; y < fire.rows(); ++y)
			{
				n += fire.cell(x, y)->burn > 0 ? 1 : 0;
			}
		}
		return n;
	}
};
} // namespace

TEST_CASE("firelogic1: FireLogicSystem.ini (table RW 0xC11750, TerrainCellType RW 0x6866CE)")
{
	FireLogicSettings s;
	std::string err;
	REQUIRE_MESSAGE(FireLogicSettings::loadText(kFireIni, s, &err), err);
	CHECK(s.maxCellsBurning == 1000);
	CHECK(s.fuelRegenerationRate == 1); // + 0x94: the constructor's (RW 0x687671)
	CHECK(s.cellTypes[0].fuel == 0u);
	CHECK(s.cellTypes[1].name == "Grass");
	CHECK(s.cellTypes[1].fuel == 100u);
	CHECK(s.cellTypes[1].maxBurnRate == 20u);
	CHECK(s.cellTypes[1].decay == 4u);
	CHECK(s.cellTypes[1].resistance == 10u);
	CHECK(s.cellTypes[2].fuel == 500u);
	// RW 0x42ECB2: a value above the row's largest is an error; RW 0x6867D2: the index must be 0 .. 3
	CHECK_FALSE(FireLogicSettings::loadText("FireLogicSystem\n  TerrainCellType 1\n    Decay = 1024\n  End\nEnd\n", s, &err));
	CHECK_FALSE(FireLogicSettings::loadText("FireLogicSystem\n  TerrainCellType 4\n  End\nEnd\n", s, &err));
}

TEST_CASE("firelogic1: the grid takes the terrain's cell type (RW 0x687748)")
{
	FireWorld f(1);
	// SyntheticTerrain 100 x 100 cells: hi 1000 -> floor(1000 * 0.1f) + 1
	CHECK(f.fire.columns() == 101);
	CHECK(f.fire.rows() == 101);
	const FireLogicSystem::Cell *c = f.fire.cell(50, 50);
	REQUIRE(c);
	CHECK(c->fuel == 100);
	CHECK(c->maxBurn == 20);
	CHECK(c->decay == 4);
	CHECK(c->resistance == 10);
	CHECK(c->burn == 0);
	CHECK_FALSE(f.fire.touched());
	FireWorld none(0);
	CHECK(none.fire.cell(50, 50)->fuel == 0);
}

TEST_CASE("firelogic1: a circle lights, burns, spreads and burns out (RW 0x6878D7, update RW 0x6886E0)")
{
	FireWorld f(1);
	const std::uint32_t before = f.w.hash();
	f.fire.changeBurnRateInArea(Coord3D{ 505.0f, 505.0f, 0.0f }, 5.0f, 15, false); // ceil(0.5) = 1 cell around floor(50.5 + 0.5) = 51
	CHECK(f.fire.touched());
	const FireLogicSystem::Cell *c = f.fire.cell(51, 51);
	CHECK(c->burn == 15);
	CHECK(c->lit);
	CHECK(f.fire.burning().empty()); // the next pass of its tenth puts it in the set
	CHECK(f.w.hash() != before);
	const int lit0 = f.litCells();
	CHECK(lit0 == 5); // rad 1: the centre row of three and one cell above and below
	f.w.frames(10); // every tenth once
	CHECK(f.fire.burning().size() >= (size_t)lit0); // tolerance: the spread's own cells join the five; the rule, not their number, is under test
	CHECK(f.litCells() > lit0); // tolerance: burn 15 - decay 4 = 11 > resistance 10: some neighbours get heat (how many depends on the tenths' order)
	CHECK(f.fire.cell(51, 51)->fuel < 100); // tolerance: it burnt; how much depends on the heat its neighbours gave back
}

TEST_CASE("firelogic1: a grass patch burns out and regrows its fuel once (RW 0x688B16 .. 0x688B80)")
{
	CombatWorld w("", 100);
	FireLogicSystem &fire = w.logic->fireLogic();
	FireLogicSettings s;
	std::string err;
	REQUIRE_MESSAGE(FireLogicSettings::loadText(kFireIni, s, &err), err);
	w.terrain.fireType = 1;
	w.terrain.fireRadius = 40.0f; // a patch of grass, fire resistant around it
	w.terrain.fireCentreX = 515.0f;
	w.terrain.fireCentreY = 515.0f;
	fire.configure(s, nullptr);
	fire.newMap(&w.terrain);
	CHECK(fire.cell(51, 51)->fuel == 100);
	CHECK(fire.cell(56, 51)->fuel == 0);
	fire.changeBurnRateInArea(Coord3D{ 505.0f, 505.0f, 0.0f }, 5.0f, 15, false);
	auto lit = [&] {
		int n = 0;
		for (int x = 40; x < 62; ++x)
			for (int y = 40; y < 62; ++y)
				n += fire.cell(x, y)->burn > 0 ? 1 : 0;
		return n;
	};
	const int ran = w.runUntil([&] { return lit() == 0 && fire.burning().empty(); }, 3000);
	CHECK(ran < 3000); // tolerance: the patch burns out well before
	CHECK(fire.cell(51, 51)->fuel < 100); // tolerance: a small fire goes out under its decay before the fuel is gone
	CHECK(fire.cell(51, 51)->state == 2);
}

TEST_CASE("firelogic1: an object standing in a burning cell takes LOGICAL_FIRE damage, one in the air does not (RW 0x688A0B .. 0x688A63)")
{
	FireWorld f(1);
	Object *d = f.w.unit("Dummy", 'B', 505.0f, 505.0f);
	f.w.frames(2);
	CHECK(f.fire.registrationCount(d->getID()) == 1); // radius 8 < 30: the one cell floor(50.5 + 0.5) = 51
	CHECK(f.fire.cell(51, 51)->watchers.size() == 1u);
	Object *far = f.w.unit("Dummy", 'B', 805.0f, 805.0f);
	f.w.frames(2);
	f.fire.changeBurnRateInArea(Coord3D{ 505.0f, 505.0f, 0.0f }, 5.0f, 15, false);
	const int ran = f.w.runUntil([&] { return f.w.health(d) < 100.0f; }, 12); // its column's tenth puts the cell in the burning set
	CHECK(ran < 12); // tolerance: within one round of the ten tenths
	CHECK(f.w.health(d) > 0.0f);
	CHECK(f.w.health(far) == 100.0f);
	CHECK(f.fire.damageCalls() > 0u); // tolerance: one per burning frame since its cell joined the set (counted above by the health)
	// it moves away: its cell changes (RW 0x69355D: registered again)
	Coord3D away{ 805.0f, 505.0f, d->getPosition()->z };
	d->setPosition(&away);
	CHECK(f.fire.cell(51, 51)->watchers.empty());
	CHECK(f.fire.cell(81, 51)->watchers.size() == 1u);
	// it leaves the world: unregistered (RW 0x68C1E5)
	const ObjectID id = d->getID();
	f.w.logic->destroyObject(d);
	f.w.frames(2);
	CHECK(f.fire.registrationCount(id) == -1);
	CHECK(f.fire.cell(81, 51)->watchers.empty());
}

TEST_CASE("firelogic1: the nugget lights a circle or the arc ahead of its source; LogicType 1 puts out, 2 adds fuel (RW 0x9120E6)")
{
	FireWorld f(1);
	FireLogicNugget n;
	n.m_damage = 15.0f;
	n.m_radius = 25.0f; // ceil(2.5) = 3 cells
	n.m_logicType = 0;  // DamageArc pi: the circle
	f.fire.deliverNugget(n, INVALID_ID, Coord3D{ 505.0f, 505.0f, 0.0f });
	const int circle = f.litCells();
	CHECK(circle == 37); // rad 3 around (51, 51): the rows 3, 5, 7, 7, 7, 5, 3
	n.m_logicType = 1;
	f.fire.deliverNugget(n, INVALID_ID, Coord3D{ 505.0f, 505.0f, 0.0f });
	CHECK(f.litCells() == 0);

	// the arc: a source at (505, 505) facing +x, DamageArc 0.5 -> only cells whose centre lies ahead within cos(0.5)
	Object *src = f.w.unit("Dummy", 'A', 505.0f, 505.0f);
	n.m_logicType = 0;
	n.m_damageArc = 0.5f;
	f.fire.deliverNugget(n, src->getID(), Coord3D{ 505.0f, 505.0f, 0.0f });
	const int arc = f.litCells();
	CHECK(arc == 22); // the circle's cells whose centre is at least cos(0.5) ahead in x: x >= 51 (10 x - 500 >= 0.878)
	CHECK(f.fire.cell(53, 51)->burn == 15); // ahead
	CHECK(f.fire.cell(48, 51)->burn == 0); // behind

	// LogicType 2: ChangeFuelInArea (Damage as fuel, MinMaxBurnRate raised, MinDecay raised, MaxResistance lowered)
	FireWorld g(1);
	FireLogicNugget fuel;
	fuel.m_logicType = 2;
	fuel.m_damage = 50.0f;
	fuel.m_radius = 5.0f;
	fuel.m_minMaxBurnRate = 60;
	fuel.m_minDecay = 7;
	fuel.m_maxResistance = 3;
	g.fire.deliverNugget(fuel, INVALID_ID, Coord3D{ 505.0f, 505.0f, 0.0f });
	const FireLogicSystem::Cell *c = g.fire.cell(51, 51);
	CHECK(c->fuel == 150);
	CHECK(c->maxBurn == 60);
	CHECK(c->decay == 7);
	CHECK(c->resistance == 3);
	CHECK(g.litCells() == 0);
}

TEST_CASE("firelogic1: water puts the cell out; a fire-resistant terrain does not burn (RW 0x687059)")
{
	FireWorld f(1);
	f.w.terrain.water.push_back(pathtest::SyntheticTerrain::Water{ 500.0f, 500.0f, 520.0f, 520.0f, 5.0f });
	f.fire.changeBurnRateInArea(Coord3D{ 505.0f, 505.0f, 0.0f }, 5.0f, 15, false);
	CHECK(f.fire.cell(51, 51)->burn == 0);  // centre (515, 515) under the water
	CHECK(f.fire.cell(52, 51)->burn == 15); // centre (525, 515) dry

	FireWorld r(0);
	r.fire.changeBurnRateInArea(Coord3D{ 505.0f, 505.0f, 0.0f }, 5.0f, 15, false);
	r.w.frames(10);
	CHECK(r.litCells() == 0); // no fuel: the first pass burns it out
	CHECK(r.fire.burning().empty());
}

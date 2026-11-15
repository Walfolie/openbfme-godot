// OpenBFME. GPL-3.0.
//
// TheFireLogicSystem (RotWK RW 0xDE46A8, vtable RW 0xC118EC; fork lane FIRELOGIC-1): the terrain fire grid. One cell per 10 x 10 world units over the
// map's maximum pathfind extent, each with fuel, a burn rate and the decay / resistance / largest burn rate of its terrain cell type (FireLogicSystem.ini's
// TerrainCellType, the map's flammability plane) or of the IMMOBILE object standing on it (the template's Flammability block). Fire arrows, burning
// catapult shots, the Balrog and the dragons light it (FireLogicNugget), it spreads to the four neighbours, burns the objects that stand in burning cells
// (DAMAGE_LOGICAL_FIRE) and regrows its fuel. Read from the RotWK binary with the Open-BFME-2 decomp (BFME2 1.06) as the reading aid: its files
// Code/GameEngine/Source/GameLogic/Map/FireLogicSystem.cpp (update), FireLogicSystemObjects.cpp (the area changes, the object footprint, xfer),
// Source/Common/FireLogicSystem_ChangeFuelInArea.cpp, System/FireLogicSystem_NewMap.cpp and Object/WeaponEffects/FireLogicNuggetDoEffectPosition.cpp. Every
// RotWK function named below was read again in the RotWK disassembly (RW = BFME2 RVA + about 0x400735); the two editions agree where it is cited.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the object (ctor RW 0x6875FB): + 0x10 four cell types of 0x18 bytes (+ 0 Color, + 4 Name, + 8 Fuel, + 0xC MaxBurnRate, + 0x10 Decay, + 0x14 Resistance),
//     + 0x70 the column pointers, + 0x74 the cells (0x14 bytes each), + 0x78 the columns (x), + 0x7C the rows (y), + 0x80 the tenth being burnt, + 0x84 the
//     burning cells (a tree of cell centres ordered by x, then y: RW 0x685D9D), + 0x90 MaxCellsBurning (0x7FFFFFFF), + 0x94 FuelRegenerationRate (1),
//     + 0x98 the extra decay (0; the weather's, RW 0x71A024, here GlobalWeatherSystem::burnDecay), + 0x9C the client's burn callback (init RW 0x685EA5:
//     RW 0x6D8BA6 -> TheDisplay vslot 0x148, presentation only: not called here);
//   * a cell: + 0 its state (1 burning, 2 burnt out), + 4 fuel (u16), + 6 the burn rate (u16), + 8 decay (10 bits), resistance (8 bits at 10), the largest
//     burn rate (12 bits at 18), bit 30 lit since the last pass (not yet in the burning set), bit 31 regrowing; + 0xC the objects standing on it, + 0x10 its
//     trees (TerrainLogic RW 0x67F4C2 takes their lumber: not ported, S-5260);
//   * FireLogicSystem.ini (field table RW 0xC11750): MaxCellsBurning, FuelRegenerationRate (parseInt), TerrainCellType <0..3> (RW 0x6866CE: Color, Name,
//     Fuel <= 0xFFFF, MaxBurnRate <= 0xFFF, Decay <= 0x3FF, Resistance <= 0xFF; RW 0x42ECB2 rejects a larger value);
//   * the update runs in GameLogic::update phase 1 just before the weather (RW 0x62E8BB, vslot 0x28 RW 0x6886E0); the grid is made at the new map just
//     after the pathfinder's (RW 0x62FDB1 -> RW 0x687748).
// Not ported (stop S-191, reported by stopLines): the trees in the cells (their lumber, a burnt tree), the client's burn callback and the GPU_TERRAINFIRE
// particles, the pathfinder's burning-cell cost (S-4462), the skirmish AI base builder's burning-cell scan (S-414), the script polygon area actions and
// conditions (RW 0x6879ED / 0x686EA5), the all-cells change (RW 0x687C4C).

#pragma once

#include "GameLogic/ObjectTypes.h"
#include "Common/StateHash.h"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class ArchiveFileSystem;
class FireLogicNugget;
class GameLogic;
class INIMacroTable;
class Object;
class PathfindTerrain;
class ThingTemplate;
class Xfer;
struct Coord3D;

// FireLogicSystem.ini
struct FireLogicSettings
{
	struct CellType
	{
		std::string name;
		unsigned fuel = 0;        // + 8
		unsigned maxBurnRate = 0; // + 0xC
		unsigned decay = 0;       // + 0x10
		unsigned resistance = 0;  // + 0x14
	};
	int maxCellsBurning = 0x7FFFFFFF; // + 0x90
	int fuelRegenerationRate = 1;     // + 0x94
	std::array<CellType, 4> cellTypes{};
	bool loaded = false;

	// data\ini\firelogicsystem.ini (the FireLogicSystem block); false with the error on a missing file or a bad value
	static bool load(ArchiveFileSystem &fs, FireLogicSettings &out, std::string *error);
	// the same from text (tests)
	static bool loadText(const std::string &text, FireLogicSettings &out, std::string *error);
};

// a template's Flammability block (ThingTemplate + 0x314; the ThingTemplate constructor RW 0x73FCCC zeroes it)
struct FireFlammability
{
	unsigned fuel = 0;        // + 0x314
	unsigned maxBurnRate = 0; // + 0x318
	unsigned decay = 0;       // + 0x31C
	unsigned resistance = 0;  // + 0x320
};

class FireLogicSystem
{
public:
	struct Cell
	{
		int state = 0;              // + 0
		std::uint16_t fuel = 0;     // + 4
		std::uint16_t burn = 0;     // + 6
		std::uint16_t decay = 0;    // + 8 bits 0 .. 9
		std::uint16_t resistance = 0; // + 8 bits 10 .. 17
		std::uint16_t maxBurn = 0;  // + 8 bits 18 .. 29
		bool lit = false;           // + 8 bit 30
		bool regrow = false;        // + 8 bit 31
		std::vector<ObjectID> watchers; // + 0xC (newest first in retail; only their sum is read)
	};

	explicit FireLogicSystem(GameLogic &logic);

	// the settings and the INI macros the templates' Flammability blocks use
	void configure(const FireLogicSettings &settings, const INIMacroTable *macros);
	const FireLogicSettings &settings() const { return m_settings; }
	// RW 0x687748: the grid over the terrain (null: no grid). `terrain` must outlive the grid
	void newMap(const PathfindTerrain *terrain);
	// RW 0x6873F7: no grid, no burning cell, no object registered
	void clear();
	// RW 0x6886E0
	void update();

	// RW 0x6878D7 (delta > 0 lights, < 0 puts out)
	void changeBurnRateInArea(const Coord3D &pos, float radius, int delta, bool onlyBurning);
	// RW 0x687B12: only the cells whose centre minus `origin` has a dot product with `dir` of at least `threshold`
	void changeBurnRateInArea(const Coord3D &pos, float radius, const Coord3D &dir, float threshold, int delta, bool onlyBurning);
	// RW 0x686BF5
	void changeFuelInArea(const Coord3D &pos, float radius, int add, int maxBurn, int decay, int resistance, bool onlyBurning);
	// FireLogicNugget slot 6 (RW 0x9120E6): a shot with no victim (slots 1 and 12 are false, so a victim never gets here)
	void deliverNugget(const FireLogicNugget &n, ObjectID sourceId, const Coord3D &pos);

	// the object's cells (RW 0x688354 register, 0x68836C unregister, RW 0x687C85 the footprint)
	void registerObject(Object &obj);
	void unregisterObject(Object &obj);
	// the transform or geometry changed (RW 0x693626 / 0x68B2A6): a registered object registers again
	void objectMoved(Object &obj);

	bool hasGrid() const { return !m_cells.empty(); }
	int columns() const { return m_columns; } // + 0x78 (x)
	int rows() const { return m_rows; }       // + 0x7C (y)
	const Cell *cell(int x, int y) const;
	const std::set<std::pair<int, int>> &burning() const { return m_burning; } // cell centres
	// -1: not registered (+ 0x4BC)
	int registrationCount(ObjectID id) const;
	unsigned long long damageCalls() const { return m_damageCalls; }
	bool touched() const { return m_touched; }
	const FireFlammability &flammabilityOf(const ThingTemplate &tt);

	void crc(StateHasher &h) const;
	// the block obfme:FireLogic (the port's layout of RW 0x688384's fields: see FireLogicSystem.cpp)
	void xfer(Xfer &x);
	// after a load: every object in the world registers again (retail does not save the registrations)
	void loadPostProcess();
	std::vector<std::string> stopLines() const;

private:
	struct Registration
	{
		int count = 0;
		std::vector<std::pair<int, int>> cells;
	};
	Cell &at(int x, int y) { return m_cells[(size_t)x * (size_t)m_rows + (size_t)y]; }
	void touch(int x, int y);
	void eraseBurning(int x, int y);
	bool insertBurning(int x, int y);
	void resetCellToOriginalFlammability(int x, int y);            // RW 0x685EB0
	void changeCellToObjectFlammability(int x, int y, const ThingTemplate &tt); // RW 0x686668
	void changeBurnRateSpan(int x0, int x1, int y, int delta, bool onlyBurning); // RW 0x687059
	void changeBurnRateSpan(const Coord3D &origin, const Coord3D &dir, float threshold, int x0, int x1, int y, int delta, bool onlyBurning); // RW 0x6871E7
	void changeFuelSpan(int x0, int x1, int y, int add, int maxBurn, int decay, int resistance, bool onlyBurning); // RW 0x6864EA
	void changeBurnRate(int x, int y, Cell &c, int delta);
	bool addWatcher(ObjectID id, int x, int y);                    // RW 0x687481 (add)
	void removeWatcher(ObjectID id, int x, int y);                 // RW 0x687481 (remove)
	void footprint(Object &obj, bool add);                         // RW 0x687C85
	bool underwater(int x, int y) const;
	int cellTypeAt(int x, int y) const;

	GameLogic &m_logic;
	FireLogicSettings m_settings;
	const INIMacroTable *m_macros = nullptr;
	const PathfindTerrain *m_terrain = nullptr;
	std::vector<Cell> m_cells;
	int m_columns = 0;
	int m_rows = 0;
	int m_tenth = 0; // + 0x80
	std::set<std::pair<int, int>> m_burning;
	std::map<ObjectID, Registration> m_registered;
	std::map<const ThingTemplate *, FireFlammability> m_flammability;
	std::set<std::pair<int, int>> m_touchedCells; // the port's: the cells a burn or fuel change reached since the new map (the hash reads them; the cells an
	                                              // object's Flammability sets follow from the objects, which the hash has)
	bool m_touched = false;
	std::vector<std::pair<std::pair<int, int>, Cell>> m_loadedCells; // a load's touched cells, applied by loadPostProcess
	unsigned long long m_damageCalls = 0;
	unsigned long long m_fuelFactorTemplates = 0;
};

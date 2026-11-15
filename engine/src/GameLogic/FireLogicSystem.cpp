// OpenBFME. GPL-3.0.
// See GameLogic/FireLogicSystem.h (fork lane FIRELOGIC-1: RotWK 2.01 RW 0x685E07 .. 0x688D1E, the Open-BFME-2 decomp as the reading aid).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/FireLogicSystem.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/Thing/KindOfTokens.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/WeaponNugget.h"

#include <cstddef>
#include <cstdlib>
#include <stdexcept>

namespace
{
// ---- FireLogicSystem.ini ------------------------------------------------------------------------------------------------------------------------------
// RW 0x42ECB2 (parseUnsignedInt): a non-zero userData is the largest value accepted
void parseBounded(INI *ini, void *, void *store, const void *userData)
{
	const char *token = ini->getNextToken();
	const unsigned v = ini->scanUnsignedInt(token);
	const unsigned limit = (unsigned)(std::uintptr_t)userData;
	if (limit != 0 && v > limit)
	{
		throw INIException(3, "Value %u is larger than the largest allowed, %u", v, limit);
	}
	*(unsigned *)store = v;
}

struct ColorSink
{
	int color = 0;
};

// RW 0xDA07A8 (built by RW 0x6866CE on first use)
const FieldParse kCellTypeFields[] = {
	{ "Color", INI::parseColorInt, nullptr, -1 }, // + 0: the editor's colour (not kept)
	{ "Name", INI::parseQuotedAsciiString, nullptr, (int)offsetof(FireLogicSettings::CellType, name) },
	{ "Fuel", parseBounded, (const void *)(std::uintptr_t)0xFFFF, (int)offsetof(FireLogicSettings::CellType, fuel) },
	{ "MaxBurnRate", parseBounded, (const void *)(std::uintptr_t)0xFFF, (int)offsetof(FireLogicSettings::CellType, maxBurnRate) },
	{ "Decay", parseBounded, (const void *)(std::uintptr_t)0x3FF, (int)offsetof(FireLogicSettings::CellType, decay) },
	{ "Resistance", parseBounded, (const void *)(std::uintptr_t)0xFF, (int)offsetof(FireLogicSettings::CellType, resistance) },
	{ nullptr, nullptr, nullptr, 0 },
};

void parseColorSkip(INI *ini, void *, void *, const void *)
{
	ColorSink sink;
	INI::parseColorInt(ini, nullptr, &sink.color, nullptr);
}

const FieldParse *cellTypeFields()
{
	static FieldParse table[sizeof(kCellTypeFields) / sizeof(kCellTypeFields[0])];
	static bool built = false;
	if (!built)
	{
		for (size_t i = 0; i < sizeof(kCellTypeFields) / sizeof(kCellTypeFields[0]); ++i)
		{
			table[i] = kCellTypeFields[i];
		}
		table[0] = FieldParse{ "Color", parseColorSkip, nullptr, 0 };
		built = true;
	}
	return table;
}

// RW 0x6866CE: TerrainCellType <index 0 .. 3> (atoi of the first token), then the block into cell type <index> (system + 0x10 + 0x18 * index)
void parseTerrainCellType(INI *ini, void *instance, void *, const void *)
{
	const char *token = ini->getNextToken();
	const int index = std::atoi(token); // RW 0xBD0628
	if (index < 0 || index >= 4)
	{
		throw INIException(3, "TerrainCellType %d: the index must be 0 .. 3", index); // RW 0x6867D2
	}
	ini->initFromINI(&static_cast<FireLogicSettings *>(instance)->cellTypes[(size_t)index], cellTypeFields());
}

// RW 0xC11750
const FieldParse kSystemFields[] = {
	{ "MaxCellsBurning", INI::parseInt, nullptr, (int)offsetof(FireLogicSettings, maxCellsBurning) },             // + 0x90
	{ "FuelRegenerationRate", INI::parseInt, nullptr, (int)offsetof(FireLogicSettings, fuelRegenerationRate) },   // + 0x94
	{ "TerrainCellType", parseTerrainCellType, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

bool runSettings(INIEnvironment &env, FireLogicSettings &out, const std::string &path, const std::string *text, std::string *error)
{
	INIBlockRecorder recorder;
	RegisterRecordingBlockStubs(env.blocks, recorder, { "FireLogicSystem" }, StubExtent::LenientAfterEnd);
	env.blocks.registerBlock("FireLogicSystem", [&out](INI *ini) {
		out.loaded = true;
		ini->getNextTokenOrNull();
		ini->initFromINI(&out, kSystemFields);
	});
	INI ini(env);
	try
	{
		if (text)
		{
			ini.loadMemory(path, std::vector<std::uint8_t>(text->begin(), text->end()), INI_LOAD_OVERWRITE);
		}
		else
		{
			ini.load(path, INI_LOAD_OVERWRITE);
		}
	}
	catch (const std::exception &e)
	{
		if (error)
		{
			*error = path + ": " + e.what();
		}
		return false;
	}
	if (!out.loaded)
	{
		if (error)
		{
			*error = path + ": no FireLogicSystem block";
		}
		return false;
	}
	return true;
}

// ---- a template's Flammability block (RW 0x73D6xx: table RW 0xDA49F0, Fuel <= 0xFFFF, FuelFactor RW 0x73D0FD, MaxBurnRate <= 0xFFF, Decay <= 0x3FF,
// Resistance <= 0xFF) ----------------------------------------------------------------------------------------------------------------------------------
struct FlammabilityParse
{
	FireFlammability values;
	float maxHealth = -1.0f; // the template's first body module's MaxHealth (-1: none)
	bool fuelFactor = false;
};

// RW 0x73D0FD: the first behavior module that is a body (vslot 0x1C); none -> "should be after the Body statement" (RW 0xC26B70); fuel = _ftol2(factor *
// the body's MaxHealth (+ 8)), above 0xFFFF an error (RW 0xC26B3C)
void parseFuelFactor(INI *ini, void *instance, void *, const void *)
{
	FlammabilityParse &p = *static_cast<FlammabilityParse *>(instance);
	if (p.maxHealth < 0.0f)
	{
		throw INIException(3, "FuelFactor: the Flammability block must come after the Body module");
	}
	const float factor = ini->scanReal(ini->getNextToken());
	const long long fuel = SimMath::ftol2(SimMath::pc24MulW((double)factor, (double)p.maxHealth)); // fmul dword [body + 8], RW 0xA3CFA4
	const unsigned v = (unsigned)fuel;
	if (v > 0xFFFFu)
	{
		throw INIException(3, "FuelFactor: fuel %u is larger than 65535", v);
	}
	p.values.fuel = v;
	p.fuelFactor = true;
}

const FieldParse kFlammabilityFields[] = {
	{ "Fuel", parseBounded, (const void *)(std::uintptr_t)0xFFFF, (int)(offsetof(FlammabilityParse, values) + offsetof(FireFlammability, fuel)) },
	{ "FuelFactor", parseFuelFactor, nullptr, 0 },
	{ "MaxBurnRate", parseBounded, (const void *)(std::uintptr_t)0xFFF, (int)(offsetof(FlammabilityParse, values) + offsetof(FireFlammability, maxBurnRate)) },
	{ "Decay", parseBounded, (const void *)(std::uintptr_t)0x3FF, (int)(offsetof(FlammabilityParse, values) + offsetof(FireFlammability, decay)) },
	{ "Resistance", parseBounded, (const void *)(std::uintptr_t)0xFF, (int)(offsetof(FlammabilityParse, values) + offsetof(FireFlammability, resistance)) },
	{ nullptr, nullptr, nullptr, 0 },
};

float firstBodyMaxHealth(const ThingTemplate &tt)
{
	for (const ThingTemplate::Nugget &n : tt.behaviorModules().nuggets())
	{
		if (const ActiveBodyModuleData *body = dynamic_cast<const ActiveBodyModuleData *>(n.data.get()))
		{
			return body->m_maxHealth;
		}
	}
	return -1.0f;
}

int kindBit(const char *name)
{
	const int bit = KindOfTokens::indexOf(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("FireLogicSystem: no KindOf ") + name);
	}
	return bit;
}

// the cell (x, y)'s centre on the x87 (fild, fadd 0.5 RW 0xBD86A0, fmul 10.0 RW 0xC11818, fstp dword): exact for every grid index
float centre(int i)
{
	return (float)(((double)i + 0.5) * 10.0);
}

// RW 0x70C55D -> RW 0x70C50C(0.0) (ObjectPathfindAdapter::isSignificantlyAboveTerrain's): not CAN_CLIMB_WALLS / SHIP; the height above the ground + 0 over
// GlobalData Gravity * -9 (x87)
bool significantlyAboveTerrain(GameLogic &logic, const Object &o)
{
	static const int kClimb = KindOfTokens::indexOf("CAN_CLIMB_WALLS");
	static const int kShip = KindOfTokens::indexOf("SHIP");
	if ((kClimb >= 0 && o.isKindOf((unsigned)kClimb)) || (kShip >= 0 && o.isKindOf((unsigned)kShip)))
	{
		return false;
	}
	const Coord3D &p = *o.getPosition();
	const float above = SimMath::pc24Add(SimMath::pc24Sub(p.z, logic.getGroundHeight(p.x, p.y)), 0.0f);
	return above > SimMath::pc24Mul(logic.settings().gravity, -9.0f);
}

// floor(v * 0.1f + 0.5) (x87 under PC24, the CRT floor, fistp): RW 0x686C1A .. 0x686C61
int cellOf(float v)
{
	return SimMath::floorToInt(SimMath::pc24Add(SimMath::pc24Mul(v, 0.1f), 0.5f));
}
} // namespace

bool FireLogicSettings::load(ArchiveFileSystem &fs, FireLogicSettings &out, std::string *error)
{
	static const char kPath[] = "data\\ini\\firelogicsystem.ini";
	if (!fs.doesFileExist(kPath))
	{
		if (error)
		{
			*error = std::string("file not found in any mounted archive: ") + kPath;
		}
		return false;
	}
	INIEnvironment env;
	env.fileSystem = &fs;
	out = FireLogicSettings{};
	return runSettings(env, out, kPath, nullptr, error);
}

bool FireLogicSettings::loadText(const std::string &text, FireLogicSettings &out, std::string *error)
{
	INIEnvironment env;
	out = FireLogicSettings{};
	return runSettings(env, out, "firelogicsystem.ini", &text, error);
}

FireLogicSystem::FireLogicSystem(GameLogic &logic)
	: m_logic(logic)
{
}

void FireLogicSystem::configure(const FireLogicSettings &settings, const INIMacroTable *macros)
{
	m_settings = settings;
	m_macros = macros;
	m_flammability.clear();
}

const FireFlammability &FireLogicSystem::flammabilityOf(const ThingTemplate &tt)
{
	auto it = m_flammability.find(&tt);
	if (it != m_flammability.end())
	{
		return it->second;
	}
	FlammabilityParse p;
	std::string text;
	for (const RawBlock &b : tt.rawBlocks())
	{
		if (b.field != "Flammability")
		{
			continue;
		}
		for (const RawLine &l : b.lines)
		{
			text += l.text;
			text += '\n';
		}
	}
	if (!text.empty())
	{
		p.maxHealth = firstBodyMaxHealth(tt);
		INIEnvironment env;
		if (m_macros)
		{
			env.macros = *m_macros;
		}
		else if (const AIWorld *ai = m_logic.aiWorld())
		{
			env.macros = ai->iniMacros(); // no macros configured (a test world): TheAI's, the same retail table
		}
		static FlammabilityParse *current = nullptr;
		current = &p;
		env.blocks.registerBlock("Flammability", [](INI *ini) {
			ini->getNextTokenOrNull();
			ini->initFromINI(current, kFlammabilityFields);
		});
		INI ini(env);
		try
		{
			ini.loadMemory(tt.getName() + "#Flammability", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_CHILD_OBJECT);
		}
		catch (const std::exception &e)
		{
			current = nullptr;
			throw std::runtime_error(tt.getName() + ": Flammability: " + e.what());
		}
		current = nullptr;
		if (p.fuelFactor)
		{
			++m_fuelFactorTemplates;
		}
	}
	return m_flammability.emplace(&tt, p.values).first->second;
}

// ---- the grid -----------------------------------------------------------------------------------------------------------------------------------------
const FireLogicSystem::Cell *FireLogicSystem::cell(int x, int y) const
{
	if (x < 0 || y < 0 || x >= m_columns || y >= m_rows)
	{
		return nullptr;
	}
	return &m_cells[(size_t)x * (size_t)m_rows + (size_t)y];
}

int FireLogicSystem::cellTypeAt(int x, int y) const
{
	return m_terrain ? m_terrain->fireCellTypeAt(centre(x), centre(y)) : 0; // TheTerrainLogic vslot 0x60
}

bool FireLogicSystem::underwater(int x, int y) const
{
	return m_terrain && m_terrain->isUnderwater(centre(x), centre(y), nullptr, nullptr); // TheTerrainLogic vslot 0x4C
}

void FireLogicSystem::touch(int x, int y)
{
	m_touched = true;
	m_touchedCells.insert({ x, y });
}

void FireLogicSystem::eraseBurning(int x, int y)
{
	m_burning.erase({ x * 10 + 5, y * 10 + 5 }); // RW 0x6862B6 key, RW 0x686939 find, RW 0x6867F4 erase
}

bool FireLogicSystem::insertBurning(int x, int y)
{
	return m_burning.insert({ x * 10 + 5, y * 10 + 5 }).second; // RW 0x68825D
}

// RW 0x6873F7 (the decomp's rva00286CC4)
void FireLogicSystem::clear()
{
	m_cells.clear();
	m_columns = 0;
	m_rows = 0;
	m_tenth = 0;
	m_burning.clear();
	m_registered.clear();
	m_touchedCells.clear();
	m_touched = false;
	m_terrain = nullptr;
}

// RW 0x687748 (the decomp's NewMap)
void FireLogicSystem::newMap(const PathfindTerrain *terrain)
{
	clear();
	m_terrain = terrain;
	if (!terrain)
	{
		return;
	}
	float loX = 0.0f, loY = 0.0f, hiX = 0.0f, hiY = 0.0f;
	terrain->getMaximumPathfindExtent(loX, loY, hiX, hiY); // TheTerrainLogic vslot 0x30 (RW 0x46273E)
	m_columns = SimMath::floorToInt(SimMath::pc24Mul(hiX, 0.1f)) + 1; // CRT floor (RW 0xBD0580), + 0x78
	m_rows = SimMath::floorToInt(SimMath::pc24Mul(hiY, 0.1f)) + 1;    // + 0x7C
	if (m_columns < 1 || m_rows < 1)
	{
		m_columns = m_rows = 0;
		return;
	}
	m_cells.assign((size_t)m_columns * (size_t)m_rows, Cell{});
	for (int x = 0; x < m_columns; ++x)
	{
		for (int y = 0; y < m_rows; ++y)
		{
			Cell &c = at(x, y);
			const int t = cellTypeAt(x, y);
			if (t != 0 && t < 4)
			{
				const FireLogicSettings::CellType &m = m_settings.cellTypes[(size_t)t];
				c.fuel = (std::uint16_t)m.fuel;
				c.maxBurn = (std::uint16_t)(m.maxBurnRate & 0xFFF);
				c.decay = (std::uint16_t)(m.decay & 0x3FF);
				c.resistance = (std::uint16_t)(m.resistance & 0xFF);
			}
		}
	}
}

// RW 0x685EB0 (ResetCellToOriginalFlammability): the terrain type's values, each only in the direction that makes the cell burn less
void FireLogicSystem::resetCellToOriginalFlammability(int x, int y)
{
	Cell &c = at(x, y);
	const int t = cellTypeAt(x, y);
	// INFERENCE: a cell type byte of 4 or more reads past the four types in retail (RW 0x685F02 indexes without a test); retail maps hold 0 .. 3 only, and
	// such a byte is taken as type 0 here
	if (t != 0 && t < 4)
	{
		const FireLogicSettings::CellType &m = m_settings.cellTypes[(size_t)t];
		if ((int)m.fuel < (int)c.fuel)
		{
			c.fuel = (std::uint16_t)m.fuel;
		}
		if (m.maxBurnRate < c.maxBurn)
		{
			c.maxBurn = (std::uint16_t)(m.maxBurnRate & 0xFFF);
		}
		if (m.decay > c.decay)
		{
			c.decay = (std::uint16_t)(m.decay & 0x3FF);
		}
		if (m.resistance > c.resistance)
		{
			c.resistance = (std::uint16_t)(m.resistance & 0xFF);
		}
	}
	else
	{
		c.fuel = 0;
		c.decay = 0;
		c.resistance = 0;
		c.maxBurn = 0;
	}
	c.regrow = false; // not a touch: the objects' cell values follow from the objects (hashed with them)
}

// RW 0x686668 (ChangeCellToObjectFlammability): the template's four values (+ 0x314 .. + 0x320)
void FireLogicSystem::changeCellToObjectFlammability(int x, int y, const ThingTemplate &tt)
{
	const FireFlammability &f = flammabilityOf(tt);
	Cell &c = at(x, y);
	c.fuel = (std::uint16_t)f.fuel;
	c.decay = (std::uint16_t)(f.decay & 0x3FF);
	c.maxBurn = (std::uint16_t)(f.maxBurnRate & 0xFFF);
	c.resistance = (std::uint16_t)(f.resistance & 0xFF);
}

// the burn change of RW 0x687059 / 0x6871E7 on one cell that is above water
void FireLogicSystem::changeBurnRate(int x, int y, Cell &c, int delta)
{
	if (delta > 0)
	{
		if (c.burn == 0)
		{
			c.lit = true; // bit 30: the next pass puts it in the burning set
		}
		int burn = (int)c.burn + delta;
		if (burn > 0xFFFF)
		{
			burn = 0xFFFF;
		}
		c.burn = (std::uint16_t)burn;
	}
	else if (c.burn > 0)
	{
		if ((int)c.burn <= -delta)
		{
			if (!c.lit)
			{
				eraseBurning(x, y);
			}
			c.lit = false;
			c.burn = 0;
		}
		else
		{
			c.burn = (std::uint16_t)((int)c.burn + delta);
		}
	}
	touch(x, y);
}

// RW 0x687059 (ChangeBurnRate(x0, x1, y, delta, onlyBurning))
void FireLogicSystem::changeBurnRateSpan(int x0, int x1, int y, int delta, bool onlyBurning)
{
	if (y < 0 || y >= m_rows || x0 >= m_columns || x1 < 0)
	{
		return;
	}
	if (x0 < 0)
	{
		x0 = 0;
	}
	if (x1 >= m_columns)
	{
		x1 = m_columns - 1;
	}
	for (; x0 <= x1; ++x0)
	{
		Cell &c = at(x0, y);
		if (onlyBurning && c.burn < 1)
		{
			continue;
		}
		if (underwater(x0, y))
		{
			c.burn = 0; // RETAIL QUIRK KEPT: a burning cell keeps its burning set entry (only the pass's own tests erase one)
			touch(x0, y);
			continue;
		}
		changeBurnRate(x0, y, c, delta);
	}
}

// RW 0x6871E7 (the arc form): offset = (the cell centre - origin); x87 for x (fsub, fstp dword), SSE for y and z (0 - origin.z); dot = (dir.z * oz + dir.y *
// oy) + ox * dir.x (SSE in that order); threshold > dot skips the cell (comiss, ja). The offset's x steps by 10 (addss RW 0xBD83D8)
void FireLogicSystem::changeBurnRateSpan(const Coord3D &origin, const Coord3D &dir, float threshold, int x0, int x1, int y, int delta, bool onlyBurning)
{
	if (y < 0 || y >= m_rows || x0 >= m_columns || x1 < 0)
	{
		return;
	}
	if (x0 < 0)
	{
		x0 = 0;
	}
	if (x1 >= m_columns)
	{
		x1 = m_columns - 1;
	}
	float ox = SimMath::pc24Sub(centre(x0), origin.x);
	const float oy = SimMath::subf32(centre(y), origin.y);
	const float oz = SimMath::subf32(0.0f, origin.z);
	for (; x0 <= x1; ++x0, ox = SimMath::addf32(ox, 10.0f))
	{
		Cell &c = at(x0, y);
		if (onlyBurning && c.burn < 1)
		{
			continue;
		}
		const float dot = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dir.z, oz), SimMath::mulf32(dir.y, oy)), SimMath::mulf32(ox, dir.x));
		if (threshold > dot)
		{
			continue;
		}
		if (underwater(x0, y))
		{
			c.burn = 0;
			touch(x0, y);
			continue;
		}
		changeBurnRate(x0, y, c, delta);
	}
}

// RW 0x6864EA (the decomp's rva00285DC5): add fuel (clamped to 0 .. 0xFFFF), raise the largest burn rate and the decay, lower the resistance, state 0
void FireLogicSystem::changeFuelSpan(int x0, int x1, int y, int add, int maxBurn, int decay, int resistance, bool onlyBurning)
{
	if (y < 0 || y >= m_rows || x0 >= m_columns || x1 < 0)
	{
		return;
	}
	if (x0 < 0)
	{
		x0 = 0;
	}
	if (x1 >= m_columns)
	{
		x1 = m_columns - 1;
	}
	if (maxBurn > 0xFFF)
	{
		maxBurn = 0xFFF;
	}
	if (decay > 0x3FF)
	{
		decay = 0x3FF;
	}
	if (resistance < 0)
	{
		resistance = 0;
	}
	for (; x0 <= x1; ++x0)
	{
		if (underwater(x0, y)) // RW 0x68659D
		{
			continue;
		}
		Cell &c = at(x0, y);
		if (onlyBurning && c.burn < 1)
		{
			continue;
		}
		const int fuel = (int)c.fuel + add;
		c.fuel = (std::uint16_t)(fuel < 0 ? 0 : (fuel > 0xFFFF ? 0xFFFF : fuel));
		if ((unsigned)c.maxBurn < (unsigned)maxBurn) // jae: unsigned
		{
			c.maxBurn = (std::uint16_t)(maxBurn & 0xFFF);
		}
		if ((unsigned)c.decay < (unsigned)decay)
		{
			c.decay = (std::uint16_t)(decay & 0x3FF);
		}
		if ((unsigned)c.resistance > (unsigned)resistance) // jbe: unsigned
		{
			c.resistance = (std::uint16_t)(resistance & 0xFF);
		}
		c.state = 0;
		touch(x0, y);
	}
}

namespace
{
// RW 0x686C86 .. 0x686D1F / 0x6878D7: the midpoint circle of radius ceil(r * 0.1f) cells around floor(p * 0.1f + 0.5), one row pair per step
template <class Span>
void circleRows(const Coord3D &pos, float radius, Span span)
{
	const int cx = cellOf(pos.x);
	const int cy = cellOf(pos.y);
	const int rad = SimMath::ceilToInt(SimMath::pc24Mul(radius, 0.1f)); // CRT ceil (RW 0xBD0588)
	int y = rad;
	int distance = 0;
	int err = 2 - 2 * rad;
	int bot = cx;
	int top = cx;
	for (;;)
	{
		if (err + y > 0)
		{
			if (y == 0 && rad == 1)
			{
				++distance;
				++bot;
				--top;
			}
			span(top, bot, cy + y);
			if (y == 0)
			{
				break;
			}
			span(top, bot, cy - y);
			--y;
			err += 1 - 2 * y;
		}
		if (distance <= err)
		{
			continue;
		}
		++distance;
		++bot;
		--top;
		err += 2 * distance + 1;
	}
}
} // namespace

// RW 0x6878D7
void FireLogicSystem::changeBurnRateInArea(const Coord3D &pos, float radius, int delta, bool onlyBurning)
{
	if (!(radius > 0.0f) || delta == 0 || m_cells.empty())
	{
		return;
	}
	circleRows(pos, radius, [&](int x0, int x1, int y) { changeBurnRateSpan(x0, x1, y, delta, onlyBurning); });
}

// RW 0x687B12
void FireLogicSystem::changeBurnRateInArea(const Coord3D &pos, float radius, const Coord3D &dir, float threshold, int delta, bool onlyBurning)
{
	if (!(radius > 0.0f) || delta == 0 || m_cells.empty())
	{
		return;
	}
	circleRows(pos, radius, [&](int x0, int x1, int y) { changeBurnRateSpan(pos, dir, threshold, x0, x1, y, delta, onlyBurning); });
}

// RW 0x686BF5 (ChangeFuelInArea)
void FireLogicSystem::changeFuelInArea(const Coord3D &pos, float radius, int add, int maxBurn, int decay, int resistance, bool onlyBurning)
{
	if (!(radius > 0.0f) || add == 0 || m_cells.empty())
	{
		return;
	}
	circleRows(pos, radius, [&](int x0, int x1, int y) { changeFuelSpan(x0, x1, y, add, maxBurn, decay, resistance, onlyBurning); });
}

// RW 0x9120E6 (FireLogicNugget slot 6): by LogicType (+ 0x1C4); the amount is cvttss2si(Damage) (+ 0x148), the radius Radius (+ 0x150)
void FireLogicSystem::deliverNugget(const FireLogicNugget &n, ObjectID sourceId, const Coord3D &pos)
{
	const int amount = SimMath::cvttss2si(n.m_damage);
	switch (n.m_logicType)
	{
	case 0:
		// DamageArc (+ 0x158) in [0, pi) (RW 0xC1B594 / 0xBDD388): the source's facing (RW 0x70BA23) and the CRT cos of the arc (RW 0xA3CF84, fstp dword);
		// no source: (1, 0, 0) and 1.0
		if (n.m_damageArc >= 0.0f && n.m_damageArc < 3.14159274f)
		{
			Coord3D dir{ 1.0f, 0.0f, 0.0f };
			float threshold = 1.0f;
			if (const Object *source = sourceId != INVALID_ID ? m_logic.findObjectByID(sourceId) : nullptr)
			{
				dir.x = SimMath::cosDet(source->getOrientation());
				dir.y = SimMath::sinDet(source->getOrientation());
				threshold = SimMath::fstpDword(SimMath::cosd(n.m_damageArc));
			}
			changeBurnRateInArea(pos, n.m_radius, dir, threshold, amount, false);
		}
		else
		{
			changeBurnRateInArea(pos, n.m_radius, amount, false);
		}
		break;
	case 1:
		changeBurnRateInArea(pos, n.m_radius, SimMath::cvttss2si(SimMath::subf32(0.0f, n.m_damage)), false);
		break;
	case 2:
		changeFuelInArea(pos, n.m_radius, amount, n.m_minMaxBurnRate, n.m_minDecay, n.m_maxResistance, false);
		break;
	case 3:
		changeBurnRateInArea(pos, n.m_radius, amount, true);
		break;
	case 4:
		changeFuelInArea(pos, n.m_radius, amount, n.m_minMaxBurnRate, n.m_minDecay, n.m_maxResistance, true);
		break;
	default:
		break;
	}
}

// ---- the update (RW 0x6886E0) -------------------------------------------------------------------------------------------------------------------------
void FireLogicSystem::update()
{
	if (m_cells.empty() || m_columns < 1 || m_rows < 1)
	{
		return;
	}
	const long long extra = (long long)m_logic.weather().burnDecay(); // + 0x98
	int start = m_tenth * m_columns / 10;
	int end = (m_tenth + 1) * m_columns / 10;
	// pass 1: the tenth's burning cells, each column from its last row down
	for (int x = start; x < end; ++x)
	{
		for (int y = m_rows - 1; y >= 0; --y)
		{
			Cell &c = at(x, y);
			if (c.burn == 0)
			{
				continue;
			}
			touch(x, y);
			if ((long long)c.burn <= (long long)c.decay + extra)
			{
				if (!c.lit)
				{
					eraseBurning(x, y);
				}
				c.lit = false;
				c.burn = 0;
				if ((float)c.fuel > 0.0f) // RW 0x6880CE: the fuel as a float against 0.0 (RW 0xC1B594)
				{
					c.state = 2; // the client's callback (+ 0x9C) is not called
				}
				continue;
			}
			if (c.lit)
			{
				c.lit = false;
				if ((long long)m_burning.size() >= (long long)m_settings.maxCellsBurning)
				{
					c.burn = 0;
					continue;
				}
				insertBurning(x, y);
			}
			c.burn = (std::uint16_t)(c.burn - (std::uint16_t)((long long)c.decay + extra));
			int consumed;
			if (c.fuel <= c.burn)
			{
				consumed = c.fuel;
				eraseBurning(x, y);
				c.burn = 0;
				c.regrow = true;
				if ((float)c.fuel > 0.0f)
				{
					c.state = 2;
				}
			}
			else
			{
				consumed = c.burn;
				c.state = 1;
			}
			c.fuel = (std::uint16_t)(c.fuel - (std::uint16_t)consumed);
			// the cell's trees lose `consumed` lumber (TerrainLogic RW 0x67F4C2): the cells hold no trees here (S-5260)
		}
	}
	// the damage: every burning cell's burn rate to each object standing on it, summed per object (RW 0x9A2931's map, ordered by id)
	std::map<ObjectID, int> damage;
	for (const std::pair<int, int> &key : m_burning)
	{
		const Cell *c = cell(key.first / 10, key.second / 10);
		if (c && c->burn != 0)
		{
			for (ObjectID id : c->watchers)
			{
				damage[id] += c->burn;
			}
		}
	}
	if (!damage.empty())
	{
		for (const auto &d : damage)
		{
			Object *obj = m_logic.findObjectByID(d.first); // RW 0x449681
			if (!obj || significantlyAboveTerrain(m_logic, *obj)) // RW 0x70C55D
			{
				continue;
			}
			DamageInfo info; // RW 0x66365E
			info.m_input.m_damageType = DAMAGE_LOGICAL_FIRE; // 0x17
			info.m_input.m_deathType = DEATH_BURNED;          // 3
			info.m_input.m_amount = (float)d.second;          // cvtsi2ss
			++m_damageCalls;
			obj->attemptDamage(info); // RW 0x698E7D
		}
	}
	// pass 2: the spread over the tenth's columns and their neighbours (RETAIL QUIRK KEPT: the last column comes off when one more follows, RW 0x688A87)
	if (start > 0)
	{
		--start;
	}
	if (end + 1 < m_columns)
	{
		--end;
	}
	struct Pending
	{
		Cell *cell;
		std::uint16_t value;
	};
	static thread_local std::vector<Pending> pending; // RW 0xDE46C4
	pending.clear();
	for (int x = start; x < end; ++x)
	{
		for (int y = 0; y < m_rows; ++y)
		{
			Cell &c = at(x, y);
			if (c.fuel == 0)
			{
				if (c.regrow)
				{
					const int t = cellTypeAt(x, y);
					if (t != 0 && t < 4)
					{
						const unsigned typeFuel = m_settings.cellTypes[(size_t)t].fuel;
						// RETAIL QUIRK KEPT: the fuel regrows only while it is 0, so a cell gains FuelRegenerationRate once (RW 0x688B50 .. 0x688B80)
						if ((int)c.fuel < (int)typeFuel)
						{
							c.fuel = (std::uint16_t)(c.fuel + (std::uint16_t)m_settings.fuelRegenerationRate);
							if ((int)c.fuel >= (int)typeFuel)
							{
								c.fuel = (std::uint16_t)typeFuel;
							}
						}
						if ((int)c.fuel >= (int)typeFuel)
						{
							c.regrow = false;
						}
					}
					else
					{
						c.regrow = false;
					}
					touch(x, y);
				}
				continue;
			}
			const int resistance = c.resistance;
			int heat = 0;
			auto add = [&](const Cell &n) {
				if ((int)n.burn > resistance)
				{
					heat += (int)n.burn - resistance;
				}
			};
			if (x > 0)
			{
				add(at(x - 1, y));
			}
			if (x + 1 < m_columns)
			{
				add(at(x + 1, y));
			}
			if (y > 0)
			{
				add(at(x, y - 1));
			}
			if (y + 1 < m_rows)
			{
				add(at(x, y + 1));
			}
			if (heat <= 0)
			{
				continue;
			}
			if (c.burn == 0)
			{
				if ((long long)m_burning.size() >= (long long)m_settings.maxCellsBurning)
				{
					continue;
				}
				c.lit = false;
				insertBurning(x, y);
			}
			unsigned value = (unsigned)c.burn + (unsigned)heat;
			if (value > c.maxBurn)
			{
				value = c.maxBurn;
			}
			pending.push_back(Pending{ &c, (std::uint16_t)value });
			touch(x, y);
		}
	}
	for (const Pending &p : pending)
	{
		p.cell->burn = p.value;
	}
	if (++m_tenth >= 10)
	{
		m_tenth = 0; // then every burning cell's client record (RW 0x685E07, the GPU_TERRAINFIRE particles): the client's
	}
}

// ---- the objects (RW 0x687C85) ------------------------------------------------------------------------------------------------------------------------
bool FireLogicSystem::addWatcher(ObjectID id, int x, int y)
{
	if (x < 0 || y < 0 || x >= m_columns || y >= m_rows)
	{
		return false;
	}
	Cell &c = at(x, y);
	for (ObjectID w : c.watchers)
	{
		if (w == id)
		{
			return false;
		}
	}
	c.watchers.insert(c.watchers.begin(), id);
	return true;
}

void FireLogicSystem::removeWatcher(ObjectID id, int x, int y)
{
	if (x < 0 || y < 0 || x >= m_columns || y >= m_rows)
	{
		return;
	}
	Cell &c = at(x, y);
	for (auto it = c.watchers.begin(); it != c.watchers.end(); ++it)
	{
		if (*it != id)
		{
			continue;
		}
		c.watchers.erase(it);
		if (c.watchers.empty()) // and no tree (+ 0x10): the cells hold none here
		{
			static const int kImmobile = kindBit("IMMOBILE");
			const Object *obj = m_logic.findObjectByID(id);
			if (!obj || obj->isKindOf((unsigned)kImmobile)) // template KindOf + 0x108 bit 2
			{
				resetCellToOriginalFlammability(x, y);
			}
		}
		return;
	}
}

void FireLogicSystem::footprint(Object &obj, bool add)
{
	static const int kProjectile = kindBit("PROJECTILE"); // + 0x108 bit 25
	static const int kImmobile = kindBit("IMMOBILE");     // + 0x108 bit 2
	const ObjectID id = obj.getID();
	if (obj.isKindOf((unsigned)kProjectile))
	{
		if (add)
		{
			m_registered[id] = Registration{};
		}
		else
		{
			m_registered.erase(id);
		}
		return;
	}
	if (!add)
	{
		auto it = m_registered.find(id);
		if (it == m_registered.end())
		{
			return;
		}
		const std::vector<std::pair<int, int>> cells = std::move(it->second.cells);
		m_registered.erase(it);
		for (const std::pair<int, int> &c : cells)
		{
			removeWatcher(id, c.first, c.second);
		}
		return;
	}
	Registration reg;
	const Coord3D pos = *obj.getPosition();
	AIWorld *ai = m_logic.aiWorld();
	if ((pos.x == 0.0f && pos.y == 0.0f) || m_cells.empty() || !ai) // ucomiss: -0.0 counts as 0
	{
		m_registered[id] = reg;
		return;
	}
	const bool immobile = obj.isKindOf((unsigned)kImmobile);
	// the object's GeometryInfo as the pathfinder sees it (ObjectPathfindAdapter::getGeometry's), read without making the object a pathfinder adapter
	const PathfindGeometry &geom = obj.ownPathfindGeometry(ai->movementInfo(*obj.getTemplate()).geometry);
	if (!immobile && (geom.isSmall || geom.boundingCircleRadius() < 30.0f)) // + 0xAC, + 0xB8 against 30 (RW 0xBDAE54)
	{
		const int y = cellOf(pos.y);
		const int x = cellOf(pos.x);
		if (addWatcher(id, x, y))
		{
			reg.cells.push_back({ x, y });
		}
		reg.count = (int)reg.cells.size();
		m_registered[id] = reg;
		return;
	}
	const float angle = obj.getOrientation();
	auto note = [&](int x, int y) {
		if (addWatcher(id, x, y))
		{
			reg.cells.push_back({ x, y });
			if (immobile)
			{
				changeCellToObjectFlammability(x, y, *obj.getTemplate());
			}
		}
	};
	for (size_t i = 0; i < geom.shapeCount(); ++i)
	{
		const PathfindShape s = geom.shape(i);
		if (!s.active || s.offsetZ > 10.0f) // + 0x20, + 0x18 against 10 (RW 0xBD83D8)
		{
			continue;
		}
		Coord3D p = pos;
		PathfindGeometry::applyShapeOffset(s, angle, p); // RW 0xAD14F0
		if (s.type == PATHFIND_GEOMETRY_BOX)
		{
			// RW 0x687EB2 .. 0x688051: steps of 5 (RW 0xBDAE58) along the turned axes, ceil(radius * 0.4f) each (RW 0xBE0DE4); cell floor((v + 0.5f) * 0.1f)
			const float c = SimMath::cosDet(angle); // RW 0x42F4E0
			const float sn = SimMath::sinDet(angle); // RW 0x42F4D0
			const float rowX = SimMath::mulf32(sn, 5.0f);
			const float rowY = SimMath::subf32(0.0f, SimMath::mulf32(c, 5.0f));
			const float colX = SimMath::mulf32(c, 5.0f);
			const float colY = SimMath::mulf32(sn, 5.0f);
			const int cols = SimMath::ceilToInt(SimMath::pc24Mul(s.majorRadius, 0.4f));
			const int rows = SimMath::ceilToInt(SimMath::pc24Mul(s.minorRadius, 0.4f));
			float tx = SimMath::subf32(SimMath::subf32(p.x, SimMath::mulf32(s.majorRadius, c)), SimMath::mulf32(s.minorRadius, sn));
			float ty = SimMath::subf32(SimMath::addf32(SimMath::mulf32(s.minorRadius, c), p.y), SimMath::mulf32(s.majorRadius, sn));
			for (int r = 0; r < rows; ++r, tx = SimMath::addf32(tx, rowX), ty = SimMath::addf32(ty, rowY))
			{
				float sx = tx, sy = ty;
				for (int k = 0; k < cols; ++k, sx = SimMath::addf32(sx, colX), sy = SimMath::addf32(sy, colY))
				{
					note(SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Add(sx, 0.5f), 0.1f)),
						SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Add(sy, 0.5f), 0.1f)));
				}
			}
		}
		else
		{
			// RW 0x688076 .. 0x688219 (SSE): lo = floor((p - r) * 0.1f + 0.5f) - 1, size = r * 0.1f + 0.4f, hi = cvttss2si(lo + size * 2 + 2); a cell whose
			// centre offset (i + 0.5f - p * 0.1f) is within size
			const float radius = s.majorRadius;
			const int loX = SimMath::floorToInt(SimMath::addf32(SimMath::mulf32(SimMath::subf32(p.x, radius), 0.1f), 0.5f)) - 1;
			const int loY = SimMath::floorToInt(SimMath::addf32(SimMath::mulf32(SimMath::subf32(p.y, radius), 0.1f), 0.5f)) - 1;
			const float size = SimMath::addf32(SimMath::mulf32(radius, 0.1f), 0.4f);
			const float cx = SimMath::mulf32(p.x, 0.1f), cy = SimMath::mulf32(p.y, 0.1f);
			const float r2 = SimMath::mulf32(size, size);
			const float size2 = SimMath::mulf32(size, 2.0f);
			const int hiX = SimMath::cvttss2si(SimMath::addf32(SimMath::addf32(SimMath::sseFromInt32(loX), size2), 2.0f));
			const int hiY = SimMath::cvttss2si(SimMath::addf32(SimMath::addf32(SimMath::sseFromInt32(loY), size2), 2.0f));
			for (int y = loY; y < hiY; ++y)
			{
				const float dy = SimMath::subf32(SimMath::addf32(SimMath::sseFromInt32(y), 0.5f), cy);
				const float dy2 = SimMath::mulf32(dy, dy);
				for (int x = loX; x < hiX; ++x)
				{
					const float dx = SimMath::subf32(SimMath::addf32(SimMath::sseFromInt32(x), 0.5f), cx);
					if (!(r2 < SimMath::addf32(SimMath::mulf32(dx, dx), dy2))) // comiss, jb
					{
						note(x, y);
					}
				}
			}
		}
	}
	reg.count = (int)reg.cells.size();
	m_registered[id] = reg;
}

// RW 0x688354: only an object that is not registered (+ 0x4BC < 0)
void FireLogicSystem::registerObject(Object &obj)
{
	if (m_registered.count(obj.getID()) == 0)
	{
		footprint(obj, true);
	}
}

// RW 0x68836C: only a registered object
void FireLogicSystem::unregisterObject(Object &obj)
{
	if (m_registered.count(obj.getID()) != 0)
	{
		footprint(obj, false);
	}
}

void FireLogicSystem::objectMoved(Object &obj)
{
	if (m_registered.count(obj.getID()) != 0)
	{
		unregisterObject(obj);
		registerObject(obj);
	}
}

int FireLogicSystem::registrationCount(ObjectID id) const
{
	auto it = m_registered.find(id);
	return it == m_registered.end() ? -1 : it->second.count;
}

// ---- state --------------------------------------------------------------------------------------------------------------------------------------------
void FireLogicSystem::crc(StateHasher &h) const
{
	h.addU32(0xF1E10000u | (std::uint32_t)(m_tenth & 0xFF));
	h.addI32(m_columns);
	h.addI32(m_rows);
	h.addU32((std::uint32_t)m_burning.size());
	for (const std::pair<int, int> &k : m_burning)
	{
		h.addI32(k.first);
		h.addI32(k.second);
	}
	h.addU32((std::uint32_t)m_touchedCells.size());
	for (const std::pair<int, int> &k : m_touchedCells)
	{
		const Cell &c = m_cells[(size_t)k.first * (size_t)m_rows + (size_t)k.second];
		h.addI32(k.first);
		h.addI32(k.second);
		h.addI32(c.state);
		h.addU32((std::uint32_t)c.fuel | ((std::uint32_t)c.burn << 16));
		h.addU32((std::uint32_t)c.decay | ((std::uint32_t)c.resistance << 10) | ((std::uint32_t)c.maxBurn << 18) | (c.lit ? 1u << 30 : 0u) | (c.regrow ? 1u << 31 : 0u));
	}
}

// The port's layout (block obfme:FireLogic, version 1; retail's RW 0x688384 (1, 2) writes every cell: state, fuel, burn, decay, resistance, largest burn rate,
// the two flags, then the burning cells' centres). Here only what a burn or a fuel change reached is written, as TerrainTrees writes only its changed
// records: the grid size (it must match the map's), the tenth, the burning cells' centres and each touched cell in retail's cell field order. The other
// cells follow from the map and the objects; a load needs the game at its start (the grid as the new map made it)
void FireLogicSystem::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	std::int32_t columns = m_columns, rows = m_rows;
	x.xferInt(&columns);
	x.xferInt(&rows);
	if (columns != m_columns || rows != m_rows)
	{
		throw XferException("FireLogicSystem: the saved grid is " + std::to_string(columns) + " x " + std::to_string(rows) + ", this map's " +
			std::to_string(m_columns) + " x " + std::to_string(m_rows));
	}
	std::int32_t tenth = m_tenth;
	x.xferInt(&tenth);
	if (tenth < 0 || tenth >= 10)
	{
		throw XferException("FireLogicSystem: the tenth " + std::to_string(tenth) + " is not 0 .. 9");
	}
	m_tenth = tenth;
	auto inGrid = [&](std::int32_t cx, std::int32_t cy) { return cx >= 0 && cy >= 0 && cx < m_columns && cy < m_rows; };
	std::uint32_t count = (std::uint32_t)m_burning.size();
	x.xferUnsignedInt(&count);
	if (x.isLoading())
	{
		m_burning.clear();
		for (std::uint32_t i = 0; i < count; ++i)
		{
			std::int32_t kx = 0, ky = 0;
			x.xferInt(&kx);
			x.xferInt(&ky);
			if (!inGrid(kx / 10, ky / 10) || kx % 10 != 5 || ky % 10 != 5)
			{
				throw XferException("FireLogicSystem: a burning cell centre outside the grid");
			}
			m_burning.insert({ kx, ky });
		}
	}
	else
	{
		for (const std::pair<int, int> &k : m_burning)
		{
			std::int32_t kx = k.first, ky = k.second;
			x.xferInt(&kx);
			x.xferInt(&ky);
		}
	}
	std::uint8_t touched = m_touched ? 1 : 0;
	x.xferUnsignedByte(&touched);
	m_touched = touched != 0;
	std::uint32_t n = (std::uint32_t)m_touchedCells.size();
	x.xferUnsignedInt(&n);
	auto cellFields = [&](Cell &c) {
		std::int32_t state = c.state;
		x.xferInt(&state); // XferEnum "CellFireFuelStatus"
		x.xferUnsignedShort(&c.fuel);
		x.xferUnsignedShort(&c.burn);
		x.xferUnsignedShort(&c.decay);
		x.xferUnsignedShort(&c.resistance);
		x.xferUnsignedShort(&c.maxBurn);
		std::uint8_t lit = c.lit ? 1 : 0, regrow = c.regrow ? 1 : 0;
		x.xferUnsignedByte(&lit);
		x.xferUnsignedByte(&regrow);
		if (state < 0 || state > 2 || c.decay > 0x3FF || c.resistance > 0xFF || c.maxBurn > 0xFFF || lit > 1 || regrow > 1)
		{
			throw XferException("FireLogicSystem: a cell value out of its range");
		}
		c.state = state;
		c.lit = lit != 0;
		c.regrow = regrow != 0;
	};
	if (x.isLoading())
	{
		m_touchedCells.clear();
		m_loadedCells.clear();
		for (std::uint32_t i = 0; i < n; ++i)
		{
			std::int32_t kx = 0, ky = 0;
			x.xferInt(&kx);
			x.xferInt(&ky);
			if (!inGrid(kx, ky))
			{
				throw XferException("FireLogicSystem: a touched cell outside the grid");
			}
			Cell c = at(kx, ky);
			cellFields(c);
			m_touchedCells.insert({ kx, ky });
			m_loadedCells.push_back({ { kx, ky }, c });
		}
	}
	else
	{
		for (const std::pair<int, int> &k : m_touchedCells)
		{
			std::int32_t kx = k.first, ky = k.second;
			x.xferInt(&kx);
			x.xferInt(&ky);
			Cell c = at(kx, ky);
			cellFields(c);
		}
	}
}

void FireLogicSystem::loadPostProcess()
{
	// INFERENCE: retail saves no registration (the cells' object lists, Object + 0x4BC / + 0x4C0); every object in the world registers again here, then the
	// loaded cells take their saved values over what the objects' Flammability set
	for (Cell &c : m_cells)
	{
		c.watchers.clear();
	}
	m_registered.clear();
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isInWorld() && !o->isDestroyed())
		{
			footprint(*o, true);
		}
	}
	for (const auto &l : m_loadedCells)
	{
		Cell &c = at(l.first.first, l.first.second);
		std::vector<ObjectID> watchers = std::move(c.watchers);
		c = l.second;
		c.watchers = std::move(watchers);
	}
	m_loadedCells.clear();
}

std::vector<std::string> FireLogicSystem::stopLines() const
{
	std::vector<std::string> out;
	out.push_back("[S-191] TheFireLogicSystem (fork lane FIRELOGIC-1, RW 0x6886E0): the grid, the spread, the burn damage, the nugget and the objects' cells "
		"run; not ported: the trees in the cells (their lumber, RW 0x67F4C2), the client's burn callback (+ 0x9C) and GPU_TERRAINFIRE particles (RW "
		"0x685E07), the pathfinder's burning-cell cost (S-4462), the skirmish AI base builder's scan (S-414), the script polygon actions and conditions "
		"(RW 0x6879ED / 0x686EA5) and the all-cells change (RW 0x687C4C)");
	return out;
}

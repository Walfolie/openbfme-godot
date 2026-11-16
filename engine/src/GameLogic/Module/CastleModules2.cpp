// OpenBFME. GPL-3.0.
// See GameLogic/Module/CastleModules2.h (fork lane CASTLEMOD-1).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/CastleModules2.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BeaconCommands.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kRubbleFile = "E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\Object\\Update\\RubbleRiseUpdate.cpp"; // RW 0xC69348
const char *const kRubblePhases[] = { "INITIAL", "DELAY", "BURST", "FINAL", nullptr };                                                      // RW 0xDB0ED0

// FXList / OCL = <phase> <name> [<name> ...] (RW 0x8A8A1D / 0x8A8A82)
void parseRubblePhase(INI *ini, std::vector<std::string> *lists)
{
	const int phase = INI::scanIndexList(ini->getNextToken(), kRubblePhases);
	for (const char *t = ini->getNextToken(); t; t = ini->getNextTokenOrNull())
	{
		lists[phase].push_back(t);
	}
}
void parseRubbleFX(INI *ini, void *instance, void *, const void *)
{
	parseRubblePhase(ini, static_cast<RubbleRiseUpdateModuleData *>(instance)->m_fx);
}
void parseRubbleOCL(INI *ini, void *instance, void *, const void *)
{
	parseRubblePhase(ini, static_cast<RubbleRiseUpdateModuleData *>(instance)->m_ocl);
}

#define RR_OFF(member) (int)offsetof(RubbleRiseUpdateModuleData, member)
// RW 0xC69408, then the DieMux table
const FieldParse kRubbleFields[] = {
	{ "MinRubbleRiseDelay", INI::parseDurationUnsignedInt, nullptr, RR_OFF(m_minRubbleRiseDelay) },
	{ "MaxRubbleRiseDelay", INI::parseDurationUnsignedInt, nullptr, RR_OFF(m_maxRubbleRiseDelay) },
	{ "MinBurstDelay", INI::parseDurationUnsignedInt, nullptr, RR_OFF(m_minBurstDelay) },
	{ "MaxBurstDelay", INI::parseDurationUnsignedInt, nullptr, RR_OFF(m_maxBurstDelay) },
	{ "RubbleRiseDamping", INI::parseReal, nullptr, RR_OFF(m_rubbleRiseDamping) },
	{ "RubbleHeight", INI::parseReal, nullptr, RR_OFF(m_rubbleHeight) },
	{ "MaxShudder", INI::parseReal, nullptr, RR_OFF(m_maxShudder) },
	{ "BigBurstFrequency", INI::parseInt, nullptr, RR_OFF(m_bigBurstFrequency) },
	{ "OCL", parseRubbleOCL, nullptr, 0 },
	{ "FXList", parseRubbleFX, nullptr, 0 },
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, RR_OFF(m_dieMux.m_deathTypes) },
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, RR_OFF(m_dieMux.m_exemptStatus) },
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, RR_OFF(m_dieMux.m_requiredStatus) },
	{ "DamageAmountRequired", INI::parseReal, nullptr, RR_OFF(m_dieMux.m_damageAmountRequired) },
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, RR_OFF(m_dieMux.m_minKillerAngle) },
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, RR_OFF(m_dieMux.m_maxKillerAngle) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef RR_OFF

const char *const kOclFile = "E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\Object\\Update\\OCLUpdate.cpp"; // RW 0xC67480

// RW 0x73A368: the list is stored by name (the store answers at the call)
void parseName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

// RW 0xC67420
const FieldParse kOclFields[] = {
	{ "OCL", parseName, nullptr, (int)offsetof(OCLUpdateModuleData, m_ocl) },
	{ "MinDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(OCLUpdateModuleData, m_minDelay) },
	{ "MaxDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(OCLUpdateModuleData, m_maxDelay) },
	{ "CreateAtEdge", INI::parseBool, nullptr, (int)offsetof(OCLUpdateModuleData, m_createAtEdge) },
	{ "Amount", INI::parseInt, nullptr, (int)offsetof(OCLUpdateModuleData, m_amount) },
	{ nullptr, nullptr, nullptr, 0 },
};

template <class Runtime, class Data>
void bind(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}
} // namespace

void CastleModules2::registerAll(ModuleFactory &modules)
{
	OCLUpdate::registerModule(modules);
	RubbleRiseUpdate::registerModule(modules);
}

// ---- OCLUpdate -----------------------------------------------------------------------------------------------------------------------------------------

void OCLUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kOclFields);
}

void OCLUpdate::registerModule(ModuleFactory &modules)
{
	bind<OCLUpdate, OCLUpdateModuleData>(modules, "OCLUpdate"); // RW 0x659A65
}

OCLUpdate::OCLUpdate(Thing *thing, const OCLUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

// RW 0x89EBBA
void OCLUpdate::setNextCreationFrame()
{
	GameLogic &logic = getObject()->logic();
	const int delay = logic.random().getValue((int)m_data->m_minDelay, (int)m_data->m_maxDelay, kOclFile, 0x7B); // RW 0x6D328E
	m_setFrame = logic.getFrame();
	m_nextFrame = m_setFrame + (UnsignedInt)delay;
}

// RW 0x89EC43
UpdateSleepTime OCLUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	static const unsigned kUnderConstruction = (unsigned)CombatNames::status("UNDER_CONSTRUCTION"); // status 2
	// RW 0x89EB99
	if (logic.getFrame() < m_nextFrame || obj->testStatus(kUnderConstruction))
	{
		return UPDATE_SLEEP_NONE;
	}
	if (m_nextFrame == 0)
	{
		setNextCreationFrame();
		return UPDATE_SLEEP_NONE;
	}
	setNextCreationFrame();
	Coord3D at = *obj->getPosition();
	if (m_data->m_createAtEdge)
	{
		BeaconCommands::closestEdgePoint(logic, *obj->getPosition(), at); // TheTerrainLogic vslot 0x34 (RW 0x67D949)
	}
	if (!m_data->m_ocl.empty() && AsciiStringUtil::compareNoCase(m_data->m_ocl, "None") != 0)
	{
		if (const ObjectCreationList *ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(m_data->m_ocl) : nullptr)
		{
			const Coord3D second = *obj->getPosition();
			ocl->create(logic, obj, at, &second, 0); // RW 0x5F00CA(obj, &at, &pos, 0)
		}
	}
	if (m_data->m_amount > 0)
	{
		++m_made;
		if (m_made >= m_data->m_amount)
		{
			return UPDATE_SLEEP_FOREVER;
		}
	}
	return UPDATE_SLEEP_NONE;
}

// RW 0x89EA88: x87 fild of the two unsigned differences (+ 2^32 when negative), fdivp, fsubr 1.0
float OCLUpdate::countdownPercent() const
{
	const UnsignedInt now = getObject()->logic().getFrame();
	return SimMath::fstpDword(SimMath::pc24SubW(1.0, SimMath::pc24DivW(SimMath::fildU32(m_nextFrame - now), SimMath::fildU32(m_nextFrame - m_setFrame))));
}

UnsignedInt OCLUpdate::remainingFrames() const
{
	return m_nextFrame - getObject()->logic().getFrame();
}

void OCLUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32(0x0C100000u | (std::uint32_t)m_made);
	hasher.addU32(m_nextFrame);
	hasher.addU32(m_setFrame);
}

// RW 0x89EBE8: UpdateModule's first; then version 2, + 0x20, + 0x24, + 0x28 (retail returns before the version in a light CRC: the port's state hash is
// crc above)
void OCLUpdate::xfer(Xfer &x)
{
	xferUpdateModule(x);
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_nextFrame);
	x.xferUnsignedInt(&m_setFrame);
	if (v.current >= 2)
	{
		x.xferInt(&m_made);
	}
}

// ---- RubbleRiseUpdate ----------------------------------------------------------------------------------------------------------------------------------

void RubbleRiseUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kRubbleFields);
}

void RubbleRiseUpdate::registerModule(ModuleFactory &modules)
{
	bind<RubbleRiseUpdate, RubbleRiseUpdateModuleData>(modules, "RubbleRiseUpdate"); // RW 0x659DF0
}

RubbleRiseUpdate::RubbleRiseUpdate(Thing *thing, const RubbleRiseUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x8A839C: setWakeFrame(object, UPDATE_SLEEP_FOREVER)
}

// RW 0x8A83CE
void RubbleRiseUpdate::reset()
{
	m_unused24 = 0;
	m_state = WAITING;
	m_velocity = 0.0f;
	m_nextBurst = 0;
	m_height = 0.0f;
	m_rubbleHeight = 0.0f;
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}

// RW 0x8A83F6
void RubbleRiseUpdate::onDie(const DieModuleInterface::Event &event)
{
	if (m_data->m_dieMux.isDieApplicable(*getObject(), event)) // RW 0x8D29A9
	{
		reset();
	}
}

// RW 0x8A847C: each non-empty list (the FX first, then the OCLs) gives `count` distinct random entries (line 272); every FX plays at `where`, every OCL
// that exists is made by the object at `where`
void RubbleRiseUpdate::doPhase(RubbleRisePhase phase, const Coord3D &where)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	for (int pass = 0; pass < 2; ++pass)
	{
		const std::vector<std::string> &list = pass == 0 ? m_data->m_fx[phase] : m_data->m_ocl[phase];
		const int count = pass == 0 ? m_data->m_fxCount[phase] : m_data->m_oclCount[phase];
		const int size = (int)list.size();
		if (size <= 0)
		{
			continue;
		}
		if (count > size || count > 32)
		{
			throw std::logic_error("RubbleRiseUpdate: a phase asks for more distinct entries than its list has (retail loops forever)");
		}
		int picked[32];
		for (int i = 0; i < count; ++i)
		{
			int idx;
			bool again;
			do
			{
				idx = logic.random().getValue(0, size - 1, kRubbleFile, 272);
				again = false;
				for (int j = 0; j < i; ++j)
				{
					again = again || picked[j] == idx;
				}
			} while (again);
			picked[i] = idx;
		}
		for (int i = 0; i < count; ++i)
		{
			const std::string &name = list[(size_t)picked[i]];
			if (pass == 0)
			{
				if (FXEventLog::isFXName(name))
				{
					FXEvent e = FXEventLog::objectEvent(FXEvent::POSITION_FX, "RubbleRise", logic.getFrame(), name, *obj); // RW 0x494615 doFXPos(fx, where, 0, 0, 0)
					e.position = where;
					e.hasTransform = false;
					logic.fxEvents().emit(e);
				}
			}
			else if (const ObjectCreationList *ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(name) : nullptr)
			{
				ocl->create(logic, obj, where); // RW 0x5F00CA(obj, where, 0, 0)
			}
		}
	}
}

// RW 0x8A85D0
UpdateSleepTime RubbleRiseUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const RubbleRiseUpdateModuleData *d = m_data;
	static const int kPostRubble = CombatNames::modelCondition("POST_RUBBLE"), kPostCollapse = CombatNames::modelCondition("POST_COLLAPSE");
	if (m_state == WAITING && obj->testModelCondition(kPostRubble)) // + 0x110 bit 27 (59); the drawable taken to exist
	{
		const Coord3D pos = *obj->getPosition();
		doPhase(RRPHASE_INITIAL, pos);
		m_state = RISING;
		// RW 0x8A8628 .. 0x8A8720: two CLIENT random reals (line 0x9D) for the drawable's instance matrix: client
		doPhase(RRPHASE_BURST, pos);
		const UnsignedInt now = logic.getFrame();
		m_nextBurst = now + (UnsignedInt)logic.random().getValue((int)d->m_minBurstDelay, (int)d->m_maxBurstDelay, kRubbleFile, 0xAB);
		if (!logic.settings().structureRulesLoaded)
		{
			throw std::logic_error("RubbleRiseUpdate: GameLogicSettings has no Gravity (GameData not loaded)");
		}
		// x87: (1.0 - RubbleRiseDamping) * -Gravity * 20.0 (RW 0xBD2C98, 0xC693B0: qwords)
		m_velocity = SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24MulW(SimMath::pc24SubW(1.0, d->m_rubbleRiseDamping), -(double)logic.settings().gravity), 20.0));
		// TheTerrainLogic vslot 0x1C at the object's layer (RW 0x68BBE0; the ground without an AI)
		PathfindLayerEnum layer = LAYER_GROUND;
		if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
		{
			layer = ai->adapter().getLayer();
		}
		AIWorld *world = logic.aiWorld();
		m_rubbleHeight = world && world->mapReady() ? world->pathfinder().getLayerHeight(pos.x, pos.y, layer) : pos.z;
		m_height = 0.0f;
		m_start = pos;
	}
	if (m_state != RISING)
	{
		return UPDATE_SLEEP_NONE;
	}
	const UnsignedInt now = logic.getFrame();
	m_height = SimMath::addf32(m_velocity, m_height);
	const float lo = SimMath::subf32(0.0f, d->m_maxShudder);
	const float shudderY = logic.random().getValueReal(lo, d->m_maxShudder, kRubbleFile, 0xC4); // the first draw goes to y
	const float shudderX = logic.random().getValueReal(lo, d->m_maxShudder, kRubbleFile, 0xC4);
	Coord3D p;
	p.x = SimMath::pc24Add(shudderX, m_start.x);
	p.y = SimMath::pc24Add(shudderY, m_start.y);
	p.z = SimMath::addf32(m_start.z, m_height);
	obj->setPosition(&p);
	if (now >= m_nextBurst)
	{
		if (logic.random().getValue(1, d->m_bigBurstFrequency, kRubbleFile, 0xCD) == 1)
		{
			doPhase(RRPHASE_BURST, *obj->getPosition());
		}
		else
		{
			doPhase(RRPHASE_DELAY, *obj->getPosition());
		}
		m_nextBurst += (UnsignedInt)logic.random().getValue((int)d->m_minBurstDelay, (int)d->m_maxBurstDelay, kRubbleFile, 0xD6);
	}
	if (!(m_height >= SimMath::subf32(m_rubbleHeight, m_start.z)))
	{
		return UPDATE_SLEEP_NONE;
	}
	Coord3D end{ m_start.x, m_start.y, m_rubbleHeight };
	obj->setPosition(&end);
	m_state = DONE;
	doPhase(RRPHASE_FINAL, *obj->getPosition());
	// RW 0x8A841C: the bone FX stop (not ported)
	obj->setModelConditionState(kPostRubble, false);
	obj->setModelConditionState(kPostCollapse, true);
	obj->setOrientation(obj->getOrientation()); // RW 0x70C31E
	return UPDATE_SLEEP_FOREVER;
}

void RubbleRiseUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32(0x2B1E0000u | (std::uint32_t)m_state);
	hasher.addU32(m_nextBurst);
	hasher.addFloat(m_velocity);
	hasher.addFloat(m_height);
	hasher.addFloat(m_rubbleHeight);
	hasher.addFloat(m_start.x);
	hasher.addFloat(m_start.y);
	hasher.addFloat(m_start.z);
}

// OpenBFME's layout: the version, UpdateModule's, + 0x24 .. + 0x44
void RubbleRiseUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_unused24);
	x.xferUnsignedInt(&m_nextBurst);
	x.xferInt(&m_state);
	if (m_state < WAITING || m_state > DONE)
	{
		throw XferException("RubbleRiseUpdate: the state " + std::to_string(m_state) + " is unknown");
	}
	x.xferReal(&m_velocity);
	x.xferReal(&m_height);
	x.xferReal(&m_rubbleHeight);
	x.xferReal(&m_start.x);
	x.xferReal(&m_start.y);
	x.xferReal(&m_start.z);
}

// OpenBFME. GPL-3.0.
// See GameLogic/Module/PartTheHeavensModules.h (fork PARTHEAVENS-1: RotWK 2.01 RW 0x8AFC7F .. 0x8AFF42, RW 0x8B0013 .. 0x8B0323; BFME2 decomp
// DestroyEnvironmentUpdate.cpp, PartTheHeavensUpdate.cpp, Rva004AC7C9FindModuleObject.cpp).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/PartTheHeavensModules.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char kRainOfFireStop[] = "[S-7176] DestroyEnvironmentUpdate: an ENVIRONMENT object has a RainOfFireUpdate (RW 0x8AF712), which is not ported; it is not adopted";
const char kDecalStop[] = "[S-7176] PartTheHeavensUpdate: the decal (RW 0x8B0155 / 0x8B0075, TheProjectedShadowManager) is the client's and not drawn";

// RW 0xC6B844
const FieldParse kDestroyEnvironmentFields[] = {
	{ "StartTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DestroyEnvironmentUpdateModuleData, m_startTime) },
	{ "DestructionTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DestroyEnvironmentUpdateModuleData, m_destructionTime) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xC6B8F8
const FieldParse kPartTheHeavensFields[] = {
	{ "Texture", INI::parseAsciiString, nullptr, (int)offsetof(PartTheHeavensUpdateModuleData, m_texture) },
	{ "Radius", FCurve::parse, nullptr, (int)offsetof(PartTheHeavensUpdateModuleData, m_radius) },
	{ "Color", INI::parseColorInt, nullptr, (int)offsetof(PartTheHeavensUpdateModuleData, m_color) },
	{ "Opacity", FCurve::parse, nullptr, (int)offsetof(PartTheHeavensUpdateModuleData, m_opacity) },
	{ "Angle", FCurve::parse, nullptr, (int)offsetof(PartTheHeavensUpdateModuleData, m_angle) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void DestroyEnvironmentUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kDestroyEnvironmentFields);
}

void DestroyEnvironmentUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "DestroyEnvironmentUpdate";
	typedef DestroyEnvironmentUpdateModuleData Data;
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<DestroyEnvironmentUpdate>(thing, typed);
	});
}

// RW 0x8AFCA4
DestroyEnvironmentUpdate::DestroyEnvironmentUpdate(Thing *thing, const DestroyEnvironmentUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	m_startFrame = getObject()->logic().getFrame(); // TheGameLogic + 0x40
}

// RW 0x8AFE33: the first ENVIRONMENT object whose module answers behavior interface slot 0x7C (RW 0x8AFC5F); only RainOfFireUpdate does
ObjectID DestroyEnvironmentUpdate::findEnvironment() const
{
	static const int kEnvironment = CombatNames::kindOf("ENVIRONMENT"); // 101 (RW 0x8AFE46: push 0x65)
	GameLogic &logic = getObject()->logic();
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isKindOf((unsigned)kEnvironment) && o->findModule("RainOfFireUpdate"))
		{
			logic.noteStop(kRainOfFireStop);
			return INVALID_ID;
		}
	}
	return INVALID_ID;
}

// RW 0x8AFEB5
UpdateSleepTime DestroyEnvironmentUpdate::update()
{
	if (m_objectID == INVALID_ID)
	{
		m_objectID = findEnvironment();
		if (m_objectID == INVALID_ID)
		{
			return UPDATE_SLEEP_FOREVER; // every RotWK game: no object has a RainOfFireUpdate
		}
	}
	// not reached while RainOfFireUpdate is not ported (findEnvironment adopts nothing); the frames as RW 0x8AFEDA .. 0x8AFEF6
	const unsigned elapsed = getObject()->logic().getFrame() - m_startFrame;
	if (elapsed < m_data->m_startTime)
	{
		return UPDATE_SLEEP((int)(m_data->m_startTime - elapsed));
	}
	throw std::logic_error("DestroyEnvironmentUpdate: an adopted environment object without RainOfFireUpdate's port (S-7176)");
}

void DestroyEnvironmentUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32(m_startFrame);
	hasher.addU32(m_objectID);
}

// RW 0x8AFD35
void DestroyEnvironmentUpdate::xfer(Xfer &x)
{
	xferUpdateModule(x); // RW 0x850C60
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_startFrame);
	x.xferUnsignedInt(&m_objectID); // RW 0x707887
}

// RW 0x8B0273
PartTheHeavensUpdateModuleData::PartTheHeavensUpdateModuleData()
{
	m_radius.set(0.0f, 0.0f, 0.0f, 0.0f);  // RW 0x90ADF3
	m_opacity.set(0.0f, 1.0f, 0.0f, 0.0f);
	m_angle.set(0.0f, 0.0f, 0.0f, 0.0f);
}

void PartTheHeavensUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kPartTheHeavensFields);
}

void PartTheHeavensUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "PartTheHeavensUpdate";
	typedef PartTheHeavensUpdateModuleData Data;
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<PartTheHeavensUpdate>(thing, typed);
	});
}

// RW 0x8B0013
PartTheHeavensUpdate::PartTheHeavensUpdate(Thing *thing, const PartTheHeavensUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	m_startFrame = getObject()->logic().getFrame(); // TheGameLogic + 0x40
}

// RW 0x8B022E: the frames since the start drive the client's decal only (made at 0 RW 0x8B0155, updated RW 0x8B0075)
UpdateSleepTime PartTheHeavensUpdate::update()
{
	if (!m_data->m_texture.empty()) // RW 0x8B017C: without a Texture no decal is made
	{
		getObject()->logic().noteStop(kDecalStop);
	}
	return UPDATE_SLEEP_NONE;
}

void PartTheHeavensUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32(m_startFrame);
}

// RW 0x8B0125
void PartTheHeavensUpdate::xfer(Xfer &x)
{
	xferUpdateModule(x); // RW 0x850C60
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_startFrame);
}

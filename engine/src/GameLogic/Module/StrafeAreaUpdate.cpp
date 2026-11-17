// OpenBFME. GPL-3.0.
// See GameLogic/Module/StrafeAreaUpdate.h (fork STRAFE-1: RotWK 2.01 RW 0x7A80B1 .. 0x7A871B; BFME2 decomp StrafeAreaUpdate.cpp, StrafeAreaUpdate_update.cpp).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/StrafeAreaUpdate.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/BeaconCommands.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Weapon.h"

#include <cstddef>
#include <stdexcept>

namespace
{
typedef StrafeAreaUpdateModuleData Data;

// RW 0xC31968
const FieldParse kFields[] = {
	{ "WeaponName", INI::parseAsciiString, nullptr, (int)offsetof(Data, m_weaponName) },
	{ "StrafeAreaRadius", INI::parseReal, nullptr, (int)offsetof(Data, m_radius) },
	{ "Sweepfrequency", INI::parseReal, nullptr, (int)offsetof(Data, m_frequency) },
	{ "SweepAmplitude", INI::parseReal, nullptr, (int)offsetof(Data, m_amplitude) },
	{ "Slope", INI::parseReal, nullptr, (int)offsetof(Data, m_slope) },
	{ "InitialSweepPhase", INI::parseReal, nullptr, (int)offsetof(Data, m_initialPhase) },
	{ nullptr, nullptr, nullptr, 0 }
};

// AI vslot 0x188: only GiantBirdAIUpdate answers (RW 0x8CEF91, the AI itself; INFERENCE as for vslot 0x184 in WeaponDelivery: the others null)
GiantBirdAIUpdate *aircraftOf(Object &obj)
{
	return dynamic_cast<GiantBirdAIUpdate *>(obj.getAIUpdateInterface()); // + 0x260
}

// RW 0x403175 Coord3D::normalize: len = (float)RW 0x403111; unless 0, each component times (1 / len) (SSE)
void normalize(Coord3D &v)
{
	const float len = (float)SimMath::length3d(v.x, v.y, v.z);
	if (len == 0.0f)
	{
		return;
	}
	const float inv = SimMath::divf32(1.0f, len);
	v.x = SimMath::mulf32(v.x, inv);
	v.y = SimMath::mulf32(v.y, inv);
	v.z = SimMath::mulf32(v.z, inv);
}

int attackingBit()
{
	static const int kAttacking = AIUpdateInterface::modelConditionBit("ATTACKING"); // 37: + 0x110 bit 5
	return kAttacking;
}
} // namespace

void StrafeAreaUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void StrafeAreaUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "StrafeAreaUpdate";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<StrafeAreaUpdate>(thing, typed);
	});
}

// RW 0x7A80B1: every field 0, awake from the next frame
StrafeAreaUpdate::StrafeAreaUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

// RW 0x7A82AF
bool StrafeAreaUpdate::startStrafe(const Coord3D &from, const Coord3D &to)
{
	Object *obj = getObject();
	GiantBirdAIUpdate *aircraft = aircraftOf(*obj);
	if (!aircraft)
	{
		return false;
	}
	m_target = to;
	m_start = from;
	m_phase = m_data->m_initialPhase; // data + 0x1C
	m_state = 0;
	m_started = false;
	const float dx = SimMath::subf32(m_target.x, m_start.x), dy = SimMath::subf32(m_target.y, m_start.y);
	obj->setOrientation(SimMath::fstpDword(SimMath::atan2d(dy, dx))); // RW 0x441BF4(dy, dx), RW 0x70C31E
	aircraft->aiMoveToPosition(m_target, CMD_FROM_AI);                // RW 0x66C4CA(target, 2)
	return true;
}

// RW 0x7A8368
UpdateSleepTime StrafeAreaUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	GiantBirdAIUpdate *aircraft = aircraftOf(*obj);
	if (!aircraft)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const Coord3D &pos = *obj->getPosition();
	const float d = (float)SimMath::length3d(SimMath::subf32(m_target.x, pos.x), SimMath::subf32(m_target.y, pos.y), 0.0f); // RW 0x403111, fstp
	const float radius = m_data->m_radius, slope = m_data->m_slope;
	const int attacking = attackingBit();
	switch (m_state)
	{
	case 0:
		aircraft->chooseLocomotorSet(LOCOMOTORSET_SUPERSONIC); // vslot 0x238 (6)
		if (SimMath::addf32(SimMath::mulf32(slope, 2.0f), radius) >= d)
		{
			obj->setModelConditionState(attacking, true);
		}
		if (SimMath::addf32(slope, radius) >= d)
		{
			aircraft->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
			m_state = 1;
			m_nextFire = logic.getFrame() + 12;
		}
		break;
	case 1:
	{
		const WeaponTemplate *weapon = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(m_data->m_weaponName) : nullptr; // RW 0x6CC5DF
		if (weapon && logic.getFrame() >= m_nextFire)
		{
			// the facing (RW 0x70B9E0: the cached cos / sin of the orientation, z 0), normalised (RW 0x403175)
			Coord3D f{ SimMath::cosDet(obj->getOrientation()), SimMath::sinDet(obj->getOrientation()), 0.0f };
			normalize(f);
			const float negX = SimMath::subf32(0.0f, f.x);
			m_phase = SimMath::addf32(m_data->m_frequency, m_phase);
			const float offset = SimMath::fstpDword(SimMath::pc24MulW(SimMath::sind(m_phase), (double)m_data->m_amplitude)); // CRT sin RW 0xA3CF90, fmul
			// RW 0x7A84C5 .. 0x7A854D, the retail SSE operand order
			const float ax = SimMath::addf32(pos.x, SimMath::mulf32(slope, f.x));
			const float ay = SimMath::addf32(pos.y, SimMath::mulf32(slope, f.y));
			Coord3D at;
			at.x = SimMath::addf32(SimMath::mulf32(offset, f.y), ax);
			at.y = SimMath::addf32(SimMath::mulf32(negX, offset), ay);
			at.z = logic.getGroundHeight(at.x, at.y); // TerrainLogic slot 0x18
			ObjectWeapons::createAndFireTempWeapon(weapon, obj, at); // RW 0x6CF530
		}
		obj->setModelConditionState(attacking, true);
		if (m_started && d > SimMath::subf32(radius, slope))
		{
			m_state = 2;
			aircraft->chooseLocomotorSet(LOCOMOTORSET_SUPERSONIC);
			obj->setModelConditionState(attacking, false);
			break;
		}
		if (SimMath::mulf32(radius, 0.0833333358f) > d && aircraft->isIdle()) // RW 0xBD88C8, vslot 0x1B8
		{
			Coord3D onward{ SimMath::subf32(m_target.x, m_start.x), SimMath::subf32(m_target.y, m_start.y), SimMath::subf32(m_target.z, m_start.z) };
			normalize(onward);
			onward.x = SimMath::addf32(m_target.x, SimMath::mulf32(onward.x, 500.0f)); // RW 0xBDD97C
			onward.y = SimMath::addf32(m_target.y, SimMath::mulf32(onward.y, 500.0f));
			onward.z = SimMath::addf32(m_target.z, SimMath::mulf32(onward.z, 500.0f));
			Coord3D edge;
			if (!BeaconCommands::closestEdgePoint(logic, onward, edge)) // TerrainLogic slot 0x34
			{
				throw std::logic_error("StrafeAreaUpdate: no map terrain for the closest edge point (TerrainLogic slot 0x34)");
			}
			aircraft->aiMoveToPosition(edge, CMD_FROM_AI); // RW 0x66C4CA(edge, 2)
			m_started = true;
		}
		break;
	}
	case 2:
		if (d > radius && aircraft->isIdle() && !obj->isEffectivelyDead()) // + 0x458 bit 0
		{
			obj->kill(DAMAGE_UNRESISTABLE, DEATH_FADED); // RW 0x698EC3(8, 0x16)
		}
		break;
	default:
		break;
	}
	return UPDATE_SLEEP_NONE;
}

void StrafeAreaUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addFloat(m_target.x);
	hasher.addFloat(m_target.y);
	hasher.addFloat(m_target.z);
	hasher.addFloat(m_start.x);
	hasher.addFloat(m_start.y);
	hasher.addFloat(m_start.z);
	hasher.addFloat(m_phase);
	hasher.addU32((std::uint32_t)m_state);
	hasher.addBool(m_started);
	hasher.addU32(m_nextFire);
}

// RW 0x7A814E: version 3, UpdateModule's, then target, start, phase, state, started (the next fire frame is version 4 only: not saved)
void StrafeAreaUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 3, 3);
	xferUpdateModule(x); // RW 0x850C60
	SnapshotXfer::coord3D(x, m_target);
	SnapshotXfer::coord3D(x, m_start);
	x.xferReal(&m_phase);
	x.xferInt(&m_state);
	x.xferBool(&m_started);
}

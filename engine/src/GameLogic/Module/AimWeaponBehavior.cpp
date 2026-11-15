// OpenBFME. GPL-3.0.
// See GameLogic/Module/AimWeaponBehavior.h (fork lane AIM-1: RotWK 2.01 RW 0x85E1C7 .. 0x85E50E).

#include "GameLogic/Module/AimWeaponBehavior.h"

#include "Common/INI.h"
#include "Common/NumericState.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace
{
using Data = AimWeaponBehaviorModuleData;

// RW 0xC58178
const FieldParse kFields[] = {
	{ "AimLowThreshold", INI::parseReal, nullptr, (int)offsetof(Data, m_aimLowThreshold) },
	{ "AimHighThreshold", INI::parseReal, nullptr, (int)offsetof(Data, m_aimHighThreshold) },
	{ "AimNearDistance", INI::parseReal, nullptr, (int)offsetof(Data, m_aimNearDistance) },
	{ "AimFarDistance", INI::parseReal, nullptr, (int)offsetof(Data, m_aimFarDistance) },
	{ nullptr, nullptr, nullptr, 0 },
};

int condition(const char *name)
{
	const int c = CombatNames::modelCondition(name);
	if (c < 0)
	{
		throw std::logic_error(std::string("AimWeaponBehavior: the model condition ") + name + " is not in the binary's table");
	}
	return c;
}

struct AimConditions
{
	int high = condition("AIM_HIGH");         // 0xB2
	int straight = condition("AIM_STRAIGHT"); // 0xB3
	int low = condition("AIM_LOW");           // 0xB4
	int nearDist = condition("AIM_NEAR");     // 0xB5
	int farDist = condition("AIM_FAR");       // 0xB6
};

const AimConditions &aim()
{
	static const AimConditions c;
	return c;
}

Object::ModelConditionBits bitsOf(std::initializer_list<int> bits)
{
	Object::ModelConditionBits b{};
	for (int bit : bits)
	{
		b[(size_t)bit >> 5] |= 1u << (bit & 31);
	}
	return b;
}
} // namespace

void AimWeaponBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void AimWeaponBehavior::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "AimWeaponBehavior"; // RW 0x657DCA
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<AimWeaponBehavior>(thing, typed);
	});
}

// RW 0x85E29A: + 0x20 = 0; the wake frame is the next frame (RW 0x850C32(1))
AimWeaponBehavior::AimWeaponBehavior(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

// RW 0x85E30B
UpdateSleepTime AimWeaponBehavior::update()
{
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAIUpdateInterface(); // + 0x260
	ObjectWeapons *weapons = obj->getWeapons();
	const Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr; // RW 0x68B58C(0)
	// the drawable (RW 0x70E013): every retail object has one (as Weapon::fireWeaponTemplate's FX block takes it, WeaponDelivery.cpp)
	if (!ai || !weapon)
	{
		return UPDATE_SLEEP_FOREVER; // 0x3FFFFFFF
	}
	const AimConditions &c = aim();
	bool changed = false;
	const unsigned now = obj->logic().getFrame();
	bool busy = ai->isStateActive(); // vslot 0x1BC
	busy |= weapon->whenPreAttackFinished() <= now && now < weapon->followThruEnd(); // RW 0x6CA2C3
	const WeaponTemplate *t = weapon->getTemplate();
	if (t && t->m_isAimingWeapon && busy) // template + 0x156
	{
		const Coord3D &target = weapons->drawTargetPosition(); // + 0x3A8
		const Coord3D &pos = *obj->getPosition();              // + 0x38
		const float dy = SimMath::subf32(target.y, pos.y);
		const float dx = SimMath::subf32(target.x, pos.x);
		const float dz = SimMath::subf32(target.z, pos.z);
		// fld dy; fmul dy; fld dx; fmul dx; faddp; fstp qword; call sqrt (RW 0xA3CF96); fstp dword
		const double sum = NumericState::pc24AddW(NumericState::pc24MulW((double)dy, (double)dy), NumericState::pc24MulW((double)dx, (double)dx));
		float d = NumericState::fstpDword(NumericState::sqrtPC24(sum));
		if (10.0f > d) // RW 0x85E3E8: fld 10.0; fcompi (ja: 10 above d)
		{
			d = 10.0f;
		}
		const float slope = SimMath::divf32(dz, d); // divss
		int pitch = c.straight;
		if (slope > m_data->m_aimHighThreshold) // comiss slope, data + 0xC; jbe
		{
			pitch = c.high;
		}
		else if (m_data->m_aimLowThreshold > slope) // comiss data + 0x8, slope; ja
		{
			pitch = c.low;
		}
		m_aimSet = true;
		if (!obj->testModelCondition(pitch))
		{
			obj->setModelConditionState(pitch, true); // RW 0x68B53C
		}
		if (m_data->m_aimNearDistance > d) // comiss data + 0x10, d; jbe
		{
			obj->clearAndSetModelConditionFlags(bitsOf({ c.farDist }), bitsOf({ c.nearDist })); // RW 0x68D607
		}
		else if (d > m_data->m_aimFarDistance) // comiss d, data + 0x14; jbe
		{
			obj->clearAndSetModelConditionFlags(bitsOf({ c.nearDist }), bitsOf({ c.farDist }));
		}
		changed = true;
	}
	if (m_aimSet && !busy)
	{
		m_aimSet = false;
		obj->clearAndSetModelConditionFlags(bitsOf({ c.nearDist, c.farDist, c.low, c.straight, c.high }), Object::ModelConditionBits{}); // RW 0x5E3B79
		changed = true;
	}
	if (changed)
	{
		obj->flushDrawableModelConditions(); // RW 0x67449C(0)
	}
	return UPDATE_SLEEP_NONE; // 1
}

void AimWeaponBehavior::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	if (m_aimSet) // hashed only when set (an object that never aimed keeps its hash)
	{
		hasher.addU32(0xA1A00001u);
	}
}

// RW 0x85E1C7: version 1, UpdateModule's (RW 0x850C60), + 0x20
void AimWeaponBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferBool(&m_aimSet);
}

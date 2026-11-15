// OpenBFME. GPL-3.0.
// See GameLogic/Module/FlingPassengerSpecialAbilityUpdate.h (fork lane FLING-1: RotWK 2.01 RW 0x898672 .. 0x8987FB).

#include "GameLogic/Module/FlingPassengerSpecialAbilityUpdate.h"

#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Weapon.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace
{
using Data = FlingPassengerSpecialAbilityUpdateModuleData;

// INI::parseWeaponTemplate (RW 0x6CBADE through TheWeaponStore): the template named by the next token, null when TheWeaponStore does not know it
void parseWarhead(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	*static_cast<const WeaponTemplate **>(store) = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(name) : nullptr;
}

// RW 0xC0556C
const FieldParse kFields[] = {
	{ "FlingPassengerVelocity", INI::parseCoord3D, nullptr, (int)offsetof(Data, m_flingPassengerVelocity) },
	{ "FlingPassengerLandingWarhead", parseWarhead, nullptr, (int)offsetof(Data, m_flingPassengerLandingWarhead) },
	{ nullptr, nullptr, nullptr, 0 },
};

int condition(const char *name)
{
	const int c = CombatNames::modelCondition(name);
	if (c < 0)
	{
		throw std::logic_error(std::string("FlingPassengerSpecialAbilityUpdate: the model condition ") + name + " is not in the binary's table");
	}
	return c;
}
} // namespace

void FlingPassengerSpecialAbilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kFields);
}

FlingPassengerSpecialAbilityUpdate::FlingPassengerSpecialAbilityUpdate(Thing *thing, const FlingPassengerSpecialAbilityUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_fling(data)
{
}

void FlingPassengerSpecialAbilityUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "FlingPassengerSpecialAbilityUpdate"; // RW 0x65981E
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<FlingPassengerSpecialAbilityUpdate>(thing, typed);
	});
}

void FlingPassengerSpecialAbilityUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	SpecialAbilityUpdate::xfer(x);
}

// RW 0x89875A
void FlingPassengerSpecialAbilityUpdate::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect(); // RW 0x853EDF
	Object *me = getObject();
	ContainModuleInterface *contain = me->getContain(); // + 0x258
	if (!contain || contain->getContainCount() == 0)  // slot 0x114(0)
	{
		return;
	}
	// slot 0x13C(0) RW 0x866367: the first rider of the list, taken out through the contain's own removal (slot 0xA4)
	const ContainModuleInterface::ContainedItemsList *list = contain->getContainedItemsList();
	Object *rider = (list && !list->empty()) ? list->front() : nullptr;
	if (!rider)
	{
		return;
	}
	contain->removeFromContain(rider);
	if (m_fling->m_flingPassengerLandingWarhead) // data + 0xDC
	{
		ObjectWeapons::createAndFireTempWeaponAt(m_fling->m_flingPassengerLandingWarhead, me, *rider); // RW 0x6CF590
	}
	if (rider->isEffectivelyDead()) // + 0x458 bit 0
	{
		static const int kDying = condition("DYING");       // 62: + 0x110 bit 30
		static const int kSplatted = condition("SPLATTED"); // 122: + 0x118 bit 26
		if (!rider->testModelCondition(kDying))
		{
			rider->setModelConditionState(kDying, true); // RW 0x68B53C
		}
		if (!rider->testModelCondition(kSplatted))
		{
			rider->setModelConditionState(kSplatted, true);
		}
		return;
	}
	if (PhysicsBehavior *physics = PhysicsBehavior::find(*rider)) // + 0x264
	{
		physics->standUp(); // RW 0x792AFF
	}
}

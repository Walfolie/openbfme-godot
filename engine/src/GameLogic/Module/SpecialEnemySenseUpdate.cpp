// OpenBFME. GPL-3.0.
// See GameLogic/Module/SpecialEnemySenseUpdate.h (fork SENSE-1: RotWK 2.01 RW 0x89D758 .. 0x89D959; BFME2 decomp SpecialEnemySenseUpdate.cpp).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SpecialEnemySenseUpdate.h"

#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace
{
typedef SpecialEnemySenseUpdateModuleData Data;

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<ObjectFilter *>(store) = std::move(f);
}

// RW 0xC07E98
const FieldParse kFields[] = {
	{ "SpecialEnemyFilter", parseFilter, nullptr, (int)offsetof(Data, m_specialEnemyFilter) },
	{ "ScanRange", INI::parseReal, nullptr, (int)offsetof(Data, m_scanRange) },
	{ "ScanInterval", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_scanInterval) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void SpecialEnemySenseUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void SpecialEnemySenseUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "SpecialEnemySenseUpdate";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<SpecialEnemySenseUpdate>(thing, typed);
	});
}

// RW 0x89D758: UpdateModule's, awake from the next frame (RW 0x850C32 with 1)
SpecialEnemySenseUpdate::SpecialEnemySenseUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

// RW 0x89D7E6
UpdateSleepTime SpecialEnemySenseUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *owner = obj->getControllingPlayer(); // RW 0x68B678
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); }); // RW 0xC10E20
	PartitionFilterFn special([&](Object &o) { return ObjectFilterMatch::allows(logic, m_data->m_specialEnemyFilter, o, owner); }); // RW 0xBE4CC8
	PartitionFilterFn enemy([obj](Object &o) { return obj->getRelationship(o) == ENEMIES; }); // RW 0xC11DC0 (flags 1, direction 0)
	const bool found = logic.partition().getClosestObject(*obj->getPosition(), m_data->m_scanRange, FROM_CENTER_2D, { &alive, &special, &enemy }) != nullptr;
	static const int kSpecialEnemyNear = AIUpdateInterface::modelConditionBit("SPECIAL_ENEMY_NEAR"); // 258: + 0x12C bit 2
	// setModelConditionState changes nothing (and redraws nothing) when the bit already has the state, as RW's test before RW 0x68B53C
	ContainModuleInterface *contain = obj->getContain(); // + 0x258
	if (contain && contain->getHordeContainInterface()) // contain slot 0x7C
	{
		if (const ContainModuleInterface::ContainedItemsList *members = contain->getContainedItemsList()) // horde slot 0x108 (INFERENCE: the contain's list)
		{
			for (Object *member : *members)
			{
				member->setModelConditionState(kSpecialEnemyNear, found);
			}
		}
	}
	else
	{
		obj->setModelConditionState(kSpecialEnemyNear, found);
	}
	return (UpdateSleepTime)m_data->m_scanInterval; // + 0x10
}

// RW 0x89D7C5: UpdateModule's, then version 1: no field of its own
void SpecialEnemySenseUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
}

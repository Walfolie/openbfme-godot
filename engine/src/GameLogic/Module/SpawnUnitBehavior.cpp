// OpenBFME. GPL-3.0. See SpawnUnitBehavior.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SpawnUnitBehavior.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>
#include <string>

void SpawnUnitBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	// RW 0x8BC981 -> 0xC6FEF0; defaults RW 0x65573A.
	static const FieldParse fields[] = {
		{ "UnitName", INI::parseAsciiString, nullptr, (int)offsetof(SpawnUnitBehaviorModuleData, m_unitName) },
		{ "UnitCommand", INI::parseAsciiString, nullptr, (int)offsetof(SpawnUnitBehaviorModuleData, m_unitCommand) },
		{ "SpawnOnce", INI::parseBool, nullptr, (int)offsetof(SpawnUnitBehaviorModuleData, m_spawnOnce) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(fields);
}

namespace
{
int condition(const char *name)
{
	const int c = CombatNames::modelCondition(name);
	if (c < 0)
	{
		throw std::logic_error(std::string("SpawnUnitBehavior: the model condition ") + name + " is not in the binary's table");
	}
	return c;
}
} // namespace

// RW 0x8BC924: + 0x24 = data SpawnOnce (+ 0x1C)
SpawnUnitBehavior::SpawnUnitBehavior(Thing *thing, const SpawnUnitBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
	, m_maySpawn(data->m_spawnOnce)
{
}

// RW 0x8BC9B0
UpdateSleepTime SpawnUnitBehavior::update()
{
	const UpdateSleepTime kSleep = (UpdateSleepTime)LOGICFRAMES_PER_SECOND; // RW 0xD9F608: 5, every path
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Player *player = obj->getControllingPlayer(); // RW 0x68B678
	if (!player || logic.getFrame() < (unsigned)LOGICFRAMES_PER_SECOND)
	{
		return kSleep;
	}
	// Player + 0x755 (auto build): always on in the port (TOGGLE_AUTO_BUILD is not ported; header)
	static const int kAwaiting = condition("AWAITING_CONSTRUCTION");      // 67: + 0x114 bit 3
	static const int kBeingBuilt = condition("ACTIVELY_BEING_CONSTRUCTED"); // 69: + 0x114 bit 5
	if (obj->testModelCondition(kAwaiting) || obj->testModelCondition(kBeingBuilt))
	{
		return kSleep;
	}
	ProductionUpdateInterface *production = obj->getProductionUpdate(); // RW 0x68C327(0)
	if (production && production->getProductionCount() > 0)          // slot 17
	{
		if (production->getProductionCount() <= 1)
		{
			return kSleep;
		}
		const ProductionEntry *first = production->firstProduction(); // slot 21
		const ThingTemplate *tt = first ? first->objectToProduce : nullptr;
		if (tt && tt->getName() == getUnitName()) // template + 0x64 against slot 0 of the + 0x20 interface (RW 0x4065AA)
		{
			production->cancelUnitCreate(first->productionID); // slot 10
			++m_cancels;
		}
		return kSleep;
	}
	const ThingTemplate *unit = logic.things().findTemplate(m_data->m_unitName); // RW 0x6D1305
	if (!unit || !logic.economy().canAffordCommandPoints(*player, *unit))        // RW 0x6A7F79
	{
		return kSleep;
	}
	const CommandButton *button = TheCommandStore && !m_data->m_unitCommand.empty() ? TheCommandStore->findCommandButton(m_data->m_unitCommand) : nullptr; // RW 0x71D6EA
	if (m_data->m_spawnOnce && !m_maySpawn)
	{
		return kSleep;
	}
	if (button)
	{
		obj->doCommandButton(button, CMD_FROM_AI, false); // RW 0x696FD2(button, 2, 0)
	}
	m_maySpawn = false;
	return kSleep;
}

void SpawnUnitBehavior::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	if (m_maySpawn) // hashed only when set (a citadel's module, SpawnOnce no, never is)
	{
		hasher.addU32(0x5A01E001u);
	}
	if (m_cancels != 0)
	{
		hasher.addU64(m_cancels);
	}
}

// RW 0x8BC8E4: version (1, 2), UpdateModule's from version 2 (RW 0x850C60), + 0x24; OpenBFME then the cancel counter. The port writes version 2 only (a
// snapshot of the former sleeping shell, S-140, is refused by the version check)
void SpawnUnitBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 2, 2);
	xferUpdateModule(x);
	x.xferBool(&m_maySpawn);
	SnapshotXfer::u64(x, m_cancels);
}

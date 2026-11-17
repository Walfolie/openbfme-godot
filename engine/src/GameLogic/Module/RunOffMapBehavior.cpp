// OpenBFME. GPL-3.0.
// See GameLogic/Module/RunOffMapBehavior.h for the sources and what is inference.

#include "GameLogic/Module/RunOffMapBehavior.h"

#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>

namespace
{
// RW 0xC59698
const FieldParse kRunOffFields[] = {
	{ "RunToLocation", INI::parseCoord2D, nullptr, (int)offsetof(RunOffMapBehaviorModuleData, m_runToLocation) },
	{ "RequiresSpecificTrigger", INI::parseBool, nullptr, (int)offsetof(RunOffMapBehaviorModuleData, m_requiresSpecificTrigger) },
	{ "RunOffMapWaypointName", INI::parseAsciiString, nullptr, (int)offsetof(RunOffMapBehaviorModuleData, m_waypointName) },
	{ "DieOnMap", INI::parseBool, nullptr, (int)offsetof(RunOffMapBehaviorModuleData, m_dieOnMap) },
	{ nullptr, nullptr, nullptr, 0 },
};
} // namespace

void RunOffMapBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kRunOffFields); // RW 0x865278
}

void RunOffMapBehavior::registerModule(ModuleFactory &modules)
{
	const char *name = "RunOffMapBehavior";
	modules.bindTypedData<RunOffMapBehaviorModuleData>(name, MODULETYPE_BEHAVIOR); // RW 0x658715
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const RunOffMapBehaviorModuleData *typed = dynamic_cast<const RunOffMapBehaviorModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<RunOffMapBehavior>(thing, typed);
	});
}

// RW 0x865321
void RunOffMapBehavior::onObjectCreated()
{
	if (!m_data->m_requiresSpecificTrigger)
	{
		triggerFlee();
	}
}

// RW 0x86540A
void RunOffMapBehavior::triggerFlee()
{
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai || !m_data)
	{
		return;
	}
	GameLogic &logic = obj->logic();
	Coord3D dest;
	const TerrainLogic *terrain = logic.terrain();
	const Waypoint *waypoint = terrain ? terrain->findWaypointByName(m_data->m_waypointName) : nullptr; // terrain slot 0x88
	if (waypoint)
	{
		dest = waypoint->location;
	}
	else
	{
		// RW 0x70B9E0: the unit direction is the (cos, sin) of the orientation
		const float dirX = SimMath::cosf32(obj->getOrientation()), dirY = SimMath::sinf32(obj->getOrientation());
		dest.x = SimMath::addf32(obj->getPosition()->x, SimMath::mulf32(dirX, 500.0f));
		dest.y = SimMath::addf32(obj->getPosition()->y, SimMath::mulf32(dirY, 500.0f));
		dest.z = logic.getGroundHeight(dest.x, dest.y); // terrain slot 0x18
	}
	AIWorld *world = logic.aiWorld();
	if (!world || !world->pathfinder().quickDoesPathExist(ai->adapter(), obj->getPosition(), &dest, nullptr)) // RW 0x6F5BB0
	{
		const ActiveBody *body = dynamic_cast<const ActiveBody *>(obj->getBodyModule());
		if (Object *killer = logic.findObjectByID(body ? body->lastDamager() : INVALID_ID))
		{
			killer->scoreTheKill(*obj, 1); // RW 0x6955BC
		}
		obj->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL); // RW 0x698EC3(8, 0)
		return;
	}
	if (m_data->m_dieOnMap)
	{
		ai->aiMoveToAndDie(dest, CMD_FROM_AI); // RW 0x86539E
	}
	else
	{
		ai->aiMoveToAndDelete(dest, CMD_FROM_AI); // RW 0x865332
	}
	static const unsigned kFlee = (unsigned)CombatNames::status("FLEE_OFF_MAP");
	static const int kRunningOffMap = CombatNames::modelCondition("RUNNING_OFF_MAP");
	obj->setStatus(kFlee, true);                         // RW 0x62684D(0x32, 1)
	obj->setModelConditionState(kRunningOffMap, true); // + 0x128 bit 28, RW 0x68B53C
}

// RW 0x9933FE
void RunOffMapBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
}

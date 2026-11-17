// OpenBFME. GPL-3.0.
// See GameLogic/Combat/ProjectileLauncher.h for the target facts.

#include "GameLogic/Combat/ProjectileLauncher.h"

#include "Common/Player.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Module/ProjectileStreamUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"

namespace
{
// fork lane STREAM-1, RW 0x90FB7A .. 0x90FC5A (the object-victim body only: slot 6, RW 0x90FC70, has no stream call): the weapon's stream object (Weapon
// + 0x5C) and its owner (Weapon + 8) both alive, the stream's ProjectileStreamUpdate (findModule RW 0x68BDA5 of the name key) is moved to the owner's
// position (RW 0x74094C) and takes the projectile (RW 0x7408A8: the owner, the projectile, the victim and its position)
void addToStream(GameLogic &logic, const ProjectileShot &shot, const Object &projectile, const Object &victim)
{
	Object *stream = logic.findObjectByID(shot.stream);
	if (!stream)
	{
		return;
	}
	const Object *owner = logic.findObjectByID(shot.weaponOwner);
	if (!owner)
	{
		return;
	}
	ProjectileStreamUpdate *update = dynamic_cast<ProjectileStreamUpdate *>(stream->findModule("ProjectileStreamUpdate"));
	if (!update)
	{
		return;
	}
	update->setPosition(*owner->getPosition());
	update->addProjectile(owner->getID(), projectile.getID(), victim.getID(), victim.getPosition());
}
} // namespace

void ObjectProjectileLauncher::launch(GameLogic &logic, const ProjectileShot &shot)
{
	Object *source = shot.source != INVALID_ID ? logic.findObjectByID(shot.source) : nullptr;
	if (!source || !shot.nugget)
	{
		return;
	}
	const std::string &name = shot.nugget->m_projectileTemplateName;
	// TARGET RW 0x90FACB (object target) / 0x90FDAA (position target):
	// ProjectileSelf selects the firing object BEFORE testing ProjectileTemplateName.
	// Leap weapons deliberately omit that name; they wake the hero's own projectile module.
	Object *projectile = source;
	if (!shot.weapon || !shot.weapon->m_projectileSelf)
	{
		if (name.empty() || name == "None" || name == "NONE")
		{
			return; // RW 0x90FAF7: no projectile template
		}
		const ThingTemplate *tt = logic.things().findTemplate(name);
		if (!tt)
		{
			logic.reportError("weapon " + (shot.weapon ? shot.weapon->getName() : std::string("?")) + ": the ProjectileNugget's projectile '" + name + "' is not an object template");
			return;
		}
		Player *owner = source->getControllingPlayer();
		Team *team = owner ? owner->getDefaultTeam() : nullptr;
		projectile = logic.newObject(tt, team, ObjectStatusMaskType{});
		if (!projectile)
		{
			logic.reportError("weapon " + (shot.weapon ? shot.weapon->getName() : std::string("?")) + ": the projectile '" + name + "' could not be created");
			return;
		}
	}
	ProjectileUpdateInterface *iface = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : projectile->modules())
	{
		iface = dynamic_cast<ProjectileUpdateInterface *>(m.get());
		if (iface)
		{
			break;
		}
	}
	const Object *victim = shot.victim != INVALID_ID ? logic.findObjectByID(shot.victim) : nullptr;
	if (!iface)
	{
		logic.destroyObject(projectile); // RW 0x90FB6C
		logic.reportError("projectile '" + name + "' has no projectile update module (BezierProjectileBehavior)");
		if (victim)
		{
			addToStream(logic, shot, *projectile, *victim); // RW 0x90FB7A: the slot 5 body goes on to the stream
		}
		return;
	}
	projectile->setProducer(source);
	int slot = shot.slot;
	const int override = shot.nugget->m_weaponLaunchBoneSlotOverride;
	if (override >= 0 && override < 6)
	{
		slot = override;
	}
	if (victim)
	{
		iface->projectileLaunchAtObjectOrPosition(victim, nullptr, source, slot, shot.barrel, shot.weapon, shot.warhead);
		addToStream(logic, shot, *projectile, *victim);
	}
	else
	{
		Coord3D pos = shot.targetPosition;
		iface->projectileLaunchAtObjectOrPosition(nullptr, &pos, source, slot, shot.barrel, shot.weapon, shot.warhead);
	}
}

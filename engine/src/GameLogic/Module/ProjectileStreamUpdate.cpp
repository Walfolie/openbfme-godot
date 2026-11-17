// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/Module/ProjectileStreamUpdate.h (fork STREAM-1: RotWK 2.01 RW 0x7407EB .. 0x740B11).

#include "GameLogic/Module/ProjectileStreamUpdate.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

void ProjectileStreamUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "ProjectileStreamUpdate";
	// the data is a plain ModuleData (RW 0x654AD2): no field of its own
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		return std::make_unique<ProjectileStreamUpdate>(thing, data);
	});
}

// RW 0x7407EB: every field 0, awake from the next frame
ProjectileStreamUpdate::ProjectileStreamUpdate(Thing *thing, const ModuleData *data)
	: UpdateModule(thing, data)
{
}

// RW 0x740AE5
UpdateSleepTime ProjectileStreamUpdate::update()
{
	cullFrontOfList();
	if (considerDying())
	{
		getObject()->logic().destroyObject(getObject()); // RW 0x62BBAB
	}
	return UPDATE_SLEEP_NONE;
}

// RW 0x7408A8
void ProjectileStreamUpdate::addProjectile(ObjectID sourceID, ObjectID newID, ObjectID victimID, const Coord3D *victimPos)
{
	if (m_owningObject == INVALID_ID)
	{
		m_owningObject = sourceID;
	}
	if (victimID != INVALID_ID)
	{
		if (victimID != m_targetObject)
		{
			m_projectileIDs[m_nextFreeIndex] = INVALID_ID; // a hole breaks the line
			m_nextFreeIndex = (m_nextFreeIndex + 1) % kMaxProjectileStream;
			m_targetObject = victimID;
		}
		m_targetPosition = Coord3D{ 0.0f, 0.0f, 0.0f };
	}
	else if (victimPos)
	{
		const Coord3D &p = *victimPos;
		if (!(m_targetPosition.x == p.x && m_targetPosition.y == p.y && m_targetPosition.z == p.z)) // RW 0x403270
		{
			m_projectileIDs[m_nextFreeIndex] = INVALID_ID;
			m_nextFreeIndex = (m_nextFreeIndex + 1) % kMaxProjectileStream;
			m_targetPosition = p;
		}
		m_targetObject = INVALID_ID;
	}
	m_projectileIDs[m_nextFreeIndex] = newID;
	m_nextFreeIndex = (m_nextFreeIndex + 1) % kMaxProjectileStream;
}

// RW 0x74094C
void ProjectileStreamUpdate::setPosition(const Coord3D &pos)
{
	getObject()->setPosition(&pos); // RW 0x70C201
}

// RW 0x7409F9: only the front is chewed off; a dead id in the middle is a break in the line
void ProjectileStreamUpdate::cullFrontOfList()
{
	GameLogic &logic = getObject()->logic();
	while (m_firstValidIndex != m_nextFreeIndex && !logic.findObjectByID(m_projectileIDs[m_firstValidIndex]))
	{
		m_firstValidIndex = (m_firstValidIndex + 1) % kMaxProjectileStream;
	}
}

// RW 0x740A31: nothing to watch and the owner gone
bool ProjectileStreamUpdate::considerDying()
{
	return m_firstValidIndex == m_nextFreeIndex && m_owningObject != INVALID_ID && !getObject()->logic().findObjectByID(m_owningObject);
}

void ProjectileStreamUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	for (ObjectID id : m_projectileIDs)
	{
		hasher.addU32(id);
	}
	hasher.addI32(m_nextFreeIndex);
	hasher.addI32(m_firstValidIndex);
	hasher.addU32(m_owningObject);
	hasher.addU32(m_targetObject);
	hasher.addFloat(m_targetPosition.x);
	hasher.addFloat(m_targetPosition.y);
	hasher.addFloat(m_targetPosition.z);
}

// RW 0x740954
void ProjectileStreamUpdate::xfer(Xfer &x)
{
	xferUpdateModule(x); // RW 0x850C60
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	for (ObjectID &id : m_projectileIDs)
	{
		x.xferUnsignedInt(&id); // RW 0x707887
	}
	x.xferInt(&m_nextFreeIndex);
	x.xferInt(&m_firstValidIndex);
	SnapshotXfer::checkRange(m_nextFreeIndex, 0, kMaxProjectileStream - 1, "a projectile stream's next free index");
	SnapshotXfer::checkRange(m_firstValidIndex, 0, kMaxProjectileStream - 1, "a projectile stream's first valid index");
	x.xferUnsignedInt(&m_owningObject);
	if (v.current >= 2)
	{
		x.xferUnsignedInt(&m_targetObject);
		SnapshotXfer::coord3D(x, m_targetPosition);
	}
}

// OpenBFME. GPL-3.0.
//
// ProjectileStreamUpdate (fork STREAM-1, package B of docs/fork/MODULE_CENSUS.md): the object that keeps a stream weapon's projectiles in a ring so the
// client can draw them as one line (RotWK 2.01 data: one template, FlamethrowerProjectileStream, the ProjectileStreamName of the weapons FireDrakeFlame and
// BabyDrakeFlame). ZH's class (ProjectileStreamUpdate.cpp, Graham Smallwood); donor: the BFME2 decomp's ProjectileStreamUpdate.cpp / ..Xfer.cpp; read
// again from RotWK's game.dat (md5 daed3668, caveat S-001), the same statements:
//   * registration RW 0x65A143 ("ProjectileStreamUpdate", RW 0xBE40E8): the data a plain ModuleData (RW 0x654AD2), the module RW 0x64F9F3 (0x8C bytes,
//     ctor RW 0x7407EB: vtables RW 0xC27408 / 0xC67300 / 0xC273FC; 20 projectile ids + 0x20, the next free index + 0x70, the first valid index + 0x74,
//     the owner + 0x78, the target object + 0x7C, the target position + 0x80, all 0).
//   * addProjectile RW 0x7408A8 (source, projectile, victim id, victim position): the owner is the first source; a victim id: a hole (id 0) in the ring
//     when it is not the stored target, then the target is it and the position 0; else a position: a hole when it differs (RW 0x403270, exact) and it
//     is stored, the target object 0; then the projectile at the next free index (each index (i + 1) % 20, RW idiv).
//   * update RW 0x740AE5: cull the front RW 0x7409F9 (while the first valid index is not the next free one and its id finds no object, the first
//     valid index moves on); then RW 0x740A31 (the ring is empty, an owner was set and it finds no object) destroys the stream object (RW 0x62BBAB).
//     Sleeps none (1).
//   * setPosition RW 0x74094C (the object's), called by ProjectileNugget::deliver for an object victim (RW 0x90FBEE, the owner's position).
//   * xfer RW 0x740954: UpdateModule's (RW 0x850C60), version 2 (RW (1, 2)), the 20 ids (RW 0x707887), the two indices, the owner, then from version 2
//     the target object and the target position. The client read (getAllPoints RW 0x740A56) is presentation (W3DProjectileStreamDraw, S-113).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/Module/UpdateModule.h"

class ModuleFactory;

class ProjectileStreamUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(ProjectileStreamUpdate)
	static constexpr int kMaxProjectileStream = 20;

	ProjectileStreamUpdate(Thing *thing, const ModuleData *data); // RW 0x7407EB
	UpdateSleepTime update() override;                            // RW 0x740AE5
	void crc(StateHasher &hasher) const override;
	void addProjectile(ObjectID sourceID, ObjectID newID, ObjectID victimID, const Coord3D *victimPos); // RW 0x7408A8
	void setPosition(const Coord3D &pos);                                                              // RW 0x74094C

	// tests
	ObjectID projectileAt(int i) const { return m_projectileIDs[i]; }
	int nextFreeIndex() const { return m_nextFreeIndex; }
	int firstValidIndex() const { return m_firstValidIndex; }
	ObjectID owningObject() const { return m_owningObject; }

	static void registerModule(ModuleFactory &modules);

private:
	void cullFrontOfList(); // RW 0x7409F9
	bool considerDying();   // RW 0x740A31

	ObjectID m_projectileIDs[kMaxProjectileStream] = {}; ///< + 0x20
	int m_nextFreeIndex = 0;                             ///< + 0x70
	int m_firstValidIndex = 0;                           ///< + 0x74
	ObjectID m_owningObject = INVALID_ID;                ///< + 0x78
	ObjectID m_targetObject = INVALID_ID;                ///< + 0x7C
	Coord3D m_targetPosition{ 0.0f, 0.0f, 0.0f };        ///< + 0x80
};

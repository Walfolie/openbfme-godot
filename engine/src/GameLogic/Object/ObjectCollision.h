// OpenBFME. GPL-3.0.
//
// An Object's collidable for TheCollisionManager (fork lane COLLIDE-1; RotWK Object + 0x70, vtable RW 0xC12368). See GameLogic/Object/CollisionManager.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * slot 0 (RW 0x68C706): the min corner pos.x - r, pos.y - r (SSE; r = GeometryInfo + 0x10, the bounding circle), pos.z - RW 0xAD1A00 (the largest active
//     sphere radius; x87); slot 1 (RW 0x68C75C): r + pos.x, r + pos.y, RW 0xAD1920 (maxHeightAbovePosition) + pos.z;
//   * slot 4 (RW 0x670216): the id; slot 5 (RW 0x68EEA5): 1 for an IMMOBILE object (template + 0x108 bit 2), else its container (+ 0x27C) or itself: a
//     container and its riders never pair, nor two IMMOBILE objects;
//   * slot 8 (RW 0x68C7B2 -> 0x68BA2A): RW 0xAD2CE0 with this object's GeometryInfo, position and angle against the other's;
//   * slot 9 (RW 0x68EEC3): nothing when + 0x4A4 is the other's id or the other's + 0x4A4 is set; else every module (+ 0x24C, in order) with a collide
//     interface (+ 0xC slot 4) meets the other, unless the object has NO_COLLISIONS (status 4, RW 0x44DDEC(4), asked per module);
//   * the record exists for an object in the world (RW 0x68E3C7 .. 0x68E411): not INERT (it never reaches the test), not NO_COLLIDE (+ 0x108 bit 30), the
//     timing benchmark's byte RW 0xDE4148 clear (never set in a game), and a HORDE (+ 0x115 bit 5) only with AIData HordesWaitForHordes (AIData + 0xB9).

#pragma once

#include "GameLogic/Object/CollisionManager.h"

class GameLogic;
class Object;

class ObjectCollidable final : public CollisionManager::Collidable
{
public:
	explicit ObjectCollidable(Object &obj) : m_obj(obj) {}
	Object &object() const { return m_obj; }

	void collisionMinCorner(Coord3D &out) const override;
	void collisionMaxCorner(Coord3D &out) const override;
	std::uint32_t collisionId() const override;
	std::uint32_t collisionGroup() const override;
	bool collisionTest(Collidable &other) override;
	void collisionRespond(Collidable &other) override;

private:
	Object &m_obj;
};

namespace ObjectCollision
{
// RW 0x68E3C7: whether the world entry gives the object a record
bool eligible(GameLogic &logic, const Object &obj);
// RW 0x68EEC3
void respond(Object &self, Object &other);
} // namespace ObjectCollision

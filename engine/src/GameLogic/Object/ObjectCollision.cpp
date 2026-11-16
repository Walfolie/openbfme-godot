// OpenBFME. GPL-3.0.
// See GameLogic/Object/ObjectCollision.h (fork lane COLLIDE-1).

#include "GameLogic/Object/ObjectCollision.h"

#include "Common/Thing/KindOfTokens.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/SalvageCrateCollide.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SimMath.h"

#include <stdexcept>

namespace
{
int kindBit(const char *name)
{
	const int bit = KindOfTokens::indexOf(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("ObjectCollision: no KindOf ") + name);
	}
	return bit;
}
} // namespace

// RW 0x68C706
void ObjectCollidable::collisionMinCorner(Coord3D &out) const
{
	const Coord3D &p = *m_obj.getPosition();
	const float r = CombatQueries::boundingCircleRadius(m_obj); // GeometryInfo + 0x10
	out.x = SimMath::subf32(p.x, r);
	out.y = SimMath::subf32(p.y, r);
	out.z = SimMath::pc24Sub(p.z, ObjectGeometry::maxHeightBelowPosition(m_obj.geometryShapes())); // fsubr: pos.z - RW 0xAD1A00
}

// RW 0x68C75C
void ObjectCollidable::collisionMaxCorner(Coord3D &out) const
{
	const Coord3D &p = *m_obj.getPosition();
	const float r = CombatQueries::boundingCircleRadius(m_obj);
	out.x = SimMath::addf32(r, p.x);
	out.y = SimMath::addf32(r, p.y);
	out.z = SimMath::pc24Add(ObjectGeometry::maxHeightAbovePosition(m_obj.geometryShapes()), p.z); // RW 0xAD1920 + pos.z
}

std::uint32_t ObjectCollidable::collisionId() const
{
	return m_obj.getID(); // RW 0x670216: Object + 0x74
}

// RW 0x68EEA5: 1 for IMMOBILE, else the container (+ 0x27C) or the object itself. Retail compares pointers; here 1 or twice an id (never 1)
std::uint32_t ObjectCollidable::collisionGroup() const
{
	static const int kImmobile = kindBit("IMMOBILE");
	if (m_obj.isKindOf((unsigned)kImmobile))
	{
		return 1u;
	}
	const Object *owner = m_obj.getContainedBy() ? m_obj.getContainedBy() : &m_obj;
	return owner->getID() * 2u;
}

// RW 0x68C7B2 -> 0x68BA2A -> RW 0xAD2CE0
bool ObjectCollidable::collisionTest(Collidable &other)
{
	Object &o = static_cast<ObjectCollidable &>(other).object();
	return NotifyCrushModules::geometriesOverlap(m_obj.geometryShapes(), *m_obj.getPosition(), m_obj.getOrientation(), o.geometryShapes(), *o.getPosition(),
		o.getOrientation());
}

void ObjectCollidable::collisionRespond(Collidable &other)
{
	ObjectCollision::respond(m_obj, static_cast<ObjectCollidable &>(other).object());
}

bool ObjectCollision::eligible(GameLogic &logic, const Object &obj)
{
	static const int kInert = kindBit("INERT"), kNoCollide = kindBit("NO_COLLIDE"), kHorde = kindBit("HORDE");
	if (obj.isKindOf((unsigned)kInert) || obj.isKindOf((unsigned)kNoCollide)) // RW 0x68E32A; + 0x108 bit 30
	{
		return false;
	}
	if (obj.isKindOf((unsigned)kHorde) && !logic.settings().hordesWaitForHordes) // + 0x115 bit 5, AIData + 0xB9
	{
		return false;
	}
	return true;
}

// RW 0x68EEC3
void ObjectCollision::respond(Object &self, Object &other)
{
	if (self.ignoreCollisionsWith() == other.getID()) // + 0x4A4
	{
		return;
	}
	if (other.ignoreCollisionsWith() != 0)
	{
		return;
	}
	static const unsigned kNoCollisions = (unsigned)CombatNames::statuses().noCollisions;
	AIWorld *ai = self.logic().aiWorld();
	// retail walks + 0x24C as it is: the live list by index (a response that replaces a module rebuilds it)
	for (size_t i = 0; i < self.collisionResponders().size(); ++i)
	{
		const Object::CollisionResponder r = self.collisionResponders()[i];
		if (self.testStatus(kNoCollisions)) // RW 0x44DDEC(4), asked for each module
		{
			continue;
		}
		switch (r.kind)
		{
		case Object::CollisionResponder::COLLIDE:
			if (CollideModuleInterface *c = r.module->getCollide())
			{
				if (ai && r.gate)
				{
					ai->friend_noteGateContact(); // statistics (AIWorld::gateContacts)
				}
				else if (ai && r.crate)
				{
					ai->friend_noteCrateContact();
				}
				c->onCollide(&other, nullptr, nullptr); // the location and normal are not computed (null)
			}
			break;
		case Object::CollisionResponder::AI:
			if (AIUpdateInterface *a = r.module->getAIUpdateInterface())
			{
				if (ai)
				{
					a->onCollide(ai->adapterFor(other)); // RW 0x66E233 (the AI's collide interface, AI + 0x24)
				}
			}
			break;
		case Object::CollisionResponder::PROJECTILE:
		{
			// BezierProjectileBehavior's collide interface (RW 0x85E52B canCollideWith, RW 0x85FDB7 handleCollision), while it flies
			BezierProjectileBehavior *b = static_cast<BezierProjectileBehavior *>(r.module);
			if (!b->flightPath().empty() && !b->hasDetonated())
			{
				CombatState::Counters &counters = self.logic().combat().counters();
				++counters.projectileContacts;
				if (b->projectileCanCollideWith(&other))
				{
					++counters.projectileContactHits;
					b->projectileHandleCollision(&other);
				}
			}
			break;
		}
		}
	}
}

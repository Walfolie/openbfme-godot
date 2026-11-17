// OpenBFME unit tests. GPL-3.0.
// Fork lane RUNOFF-1 (package B of docs/fork/MODULE_CENSUS.md) on synthetic data: RunOffMapBehavior (a riderless horse flees from its constructor: 500
// ahead of where it was created, AI command 0x38 / 0x41, FLEE_OFF_MAP, RUNNING_OFF_MAP) and the AI states 30 / 55 (AIMoveAndDeleteState: the machine is
// locked; at the end the object is destroyed, or killed with DieOnMap).

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/RunOffMapBehavior.h"
#include "GameLogic/Object/Object.h"

using namespace combattest;

namespace
{
const char *const kHorses =
	"Object ProbeRiderless\n"
	"  KindOf = CAVALRY UNATTACKABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 25\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  Behavior = RunOffMapBehavior ModuleTag_RunOff\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object ProbeRiderlessDies\n"
	"  KindOf = CAVALRY UNATTACKABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 25\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  Behavior = RunOffMapBehavior ModuleTag_RunOff\n"
	"    DieOnMap = Yes\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object ProbeTriggered\n"
	"  KindOf = CAVALRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 25\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  Behavior = RunOffMapBehavior ModuleTag_RunOff\n"
	"    RequiresSpecificTrigger = Yes\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

// runs frames until the object is gone or dead, at most `limit`; the frames run
int runUntilGone(CombatWorld &w, ObjectID id, int limit)
{
	for (int i = 0; i < limit; ++i)
	{
		Object *o = w.logic->findObjectByID(id);
		if (!o || o->isEffectivelyDead())
		{
			return i;
		}
		w.frames(1);
	}
	return limit;
}
} // namespace

TEST_CASE("runoff1: a riderless horse flees from its constructor: 500 ahead of its creation spot, state 30, locked, then destroyed (RW 0x86540A, 0x74BA18)")
{
	CombatWorld w(kHorses);
	w.combat().setAutoAcquireEnabled(false);
	Object *h = w.unit("ProbeRiderless", 'A', 300, 300);
	AIUpdateInterface *ai = h->getAIUpdateInterface();
	REQUIRE(ai);
	// the flee ran in the constructor, at the creation spot (0, 0) facing 0: the goal is (500, 0)
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_AND_DELETE);
	CHECK(ai->stateMachine().goalPosition().x == 500.0f);
	CHECK(ai->stateMachine().goalPosition().y == 0.0f);
	CHECK(ai->stateMachine().isLocked());
	CHECK(h->testStatus((unsigned)CombatNames::status("FLEE_OFF_MAP")));
	CHECK(h->testModelCondition(CombatNames::modelCondition("RUNNING_OFF_MAP")));
	CHECK(h->testModelCondition(CombatNames::modelCondition("MOVING")));
	// a player's order meets the locked machine: the horse keeps running
	ai->aiMoveToPosition(Coord3D{ 300.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_AND_DELETE);
	const ObjectID id = h->getID();
	const int ran = runUntilGone(w, id, 1500);
	CHECK(ran < 1500); // tolerance: the walk's length is the mover's; the end is checked below
	w.frames(2);
	CHECK(w.logic->findObjectByID(id) == nullptr); // destroyed, not killed
}

TEST_CASE("runoff1: with DieOnMap the horse takes state 55 and is killed at the end, the machine unlocked (RW 0x66B817, 0x752E33)")
{
	CombatWorld w(kHorses);
	w.combat().setAutoAcquireEnabled(false);
	Object *h = w.unit("ProbeRiderlessDies", 'A', 300, 300);
	AIUpdateInterface *ai = h->getAIUpdateInterface();
	REQUIRE(ai);
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_AND_DIE);
	CHECK_FALSE(h->testModelCondition(CombatNames::modelCondition("MOVING"))); // state 55 sets no model condition of its own
	CHECK(h->testModelCondition(CombatNames::modelCondition("RUNNING_OFF_MAP")));
	const ObjectID id = h->getID();
	const int ran = runUntilGone(w, id, 1500);
	CHECK(ran < 1500); // tolerance: the walk's length is the mover's; the end is checked below
	Object *dead = w.logic->findObjectByID(id);
	REQUIRE(dead);
	CHECK(dead->isEffectivelyDead());
}

TEST_CASE("runoff1: RequiresSpecificTrigger waits; triggerFlee then runs 500 along the facing from where the object stands (RW 0x865321)")
{
	CombatWorld w(kHorses);
	w.combat().setAutoAcquireEnabled(false);
	Object *h = w.unit("ProbeTriggered", 'A', 300, 300);
	AIUpdateInterface *ai = h->getAIUpdateInterface();
	REQUIRE(ai);
	w.frames(2);
	CHECK(ai->currentStateId() != (unsigned)AI_MOVE_AND_DELETE);
	CHECK_FALSE(h->testStatus((unsigned)CombatNames::status("FLEE_OFF_MAP")));
	RunOffMapBehavior *r = dynamic_cast<RunOffMapBehavior *>(h->findModule("RunOffMapBehavior"));
	REQUIRE(r);
	r->triggerFlee();
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_AND_DELETE);
	CHECK(ai->stateMachine().goalPosition().x == 800.0f);
	CHECK(ai->stateMachine().goalPosition().y == 300.0f);
}

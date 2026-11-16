// OpenBFME unit tests. GPL-3.0.
// Fork lane CASTLEMOD-1 (package A of docs/fork/MODULE_CENSUS.md) on synthetic data: GateProxyBehavior (a gate with a Proxy links the first object of that
// template both ways; the proxy forwards open / close / isOpen / isSettled and its own update sleeps; unlinked it answers false) and OCLUpdate (the
// first due frame only sets the timer; then the list is made every MinDelay .. MaxDelay frames until Amount).

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/CastleModules2.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Object/Object.h"

using namespace combattest;

namespace
{
const char *const kGates =
	"Object ProbeGateProxy\n"
	"  KindOf = IMMOBILE SELECTABLE INERT\n"
	"  Behavior = GateProxyBehavior ModuleTag_GATE\n"
	"  End\n"
	"End\n"
	"Object ProbeGate\n"
	"  KindOf = IMMOBILE STRUCTURE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = GateOpenAndCloseBehavior ModuleTag_Gate\n"
	"    ResetTimeInMilliseconds = 1000\n"
	"    Proxy = ProbeGateProxy\n"
	"  End\n"
	"End\n"
	"Object ProbeTimer\n"
	"  KindOf = IMMOBILE STRUCTURE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = RubbleRiseUpdate ModuleTag_Rubble\n"
	"    MinBurstDelay = 400\n"
	"    MaxBurstDelay = 400\n"
	"    MaxShudder = 0.5\n"
	"    BigBurstFrequency = 4\n"
	"  End\n"
	"  Behavior = OCLUpdate ModuleTag_Timer\n"
	"    OCL = OCL_None_ForTest\n"
	"    MinDelay = 1000\n"
	"    MaxDelay = 1000\n"
	"    Amount = 2\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("castlemod1: a gate links its Proxy both ways; the proxy forwards the gate slots and sleeps; an unlinked proxy answers false (RW 0x89CB19, 0x656693)")
{
	CombatWorld w(kGates);
	Object *lonely = w.unit("ProbeGateProxy", 'A', 100, 100); // made first: the gate's search finds the first in the object list
	Object *proxy = w.unit("ProbeGateProxy", 'A', 300, 300);
	Object *gate = w.unit("ProbeGate", 'A', 300, 300);
	w.frames(2);
	GateOpenAndCloseBehavior *g = dynamic_cast<GateOpenAndCloseBehavior *>(gate->findModule("GateOpenAndCloseBehavior"));
	REQUIRE(g);
	GateProxyBehavior *p = nullptr;
	GateProxyBehavior *other = nullptr;
	for (Object *o : { lonely, proxy })
	{
		GateProxyBehavior *m = dynamic_cast<GateProxyBehavior *>(o->findModule("GateProxyBehavior"));
		REQUIRE(m);
		if (m->link() == gate->getID())
		{
			p = m;
		}
		else
		{
			other = m;
		}
	}
	REQUIRE(p);
	REQUIRE(other);
	CHECK(g->link() == p->getObject()->getID());
	CHECK(other->link() == INVALID_ID);
	CHECK(GateOpenAndCloseBehavior::findGate(*p->getObject()) == p);
	// forwarded: the gate is closed and settled
	CHECK_FALSE(p->isOpen());
	CHECK(p->isSettled());
	p->open();
	CHECK(g->state() == GateOpenAndCloseBehavior::OPENING);
	w.frames(10);
	CHECK(g->isOpen());
	CHECK(p->isOpen());
	CHECK(p->state() == GateOpenAndCloseBehavior::CLOSED); // the proxy's own state never moves (its update sleeps)
	// unlinked: false, nothing
	CHECK_FALSE(other->isOpen());
	CHECK_FALSE(other->isSettled());
	other->close();
	CHECK(g->isOpen());
}

TEST_CASE("castlemod1: RubbleRiseUpdate sleeps until its object dies, waits for POST_RUBBLE, rises to the ground and ends with POST_COLLAPSE (RW 0x8A85D0)")
{
	CombatWorld w(kGates);
	Object *t = w.unit("ProbeTimer", 'A', 300, 300);
	RubbleRiseUpdate *r = dynamic_cast<RubbleRiseUpdate *>(t->findModule("RubbleRiseUpdate"));
	REQUIRE(r);
	w.frames(3);
	CHECK(r->state() == RubbleRiseUpdate::WAITING);
	static const int kPostRubble = CombatNames::modelCondition("POST_RUBBLE"), kPostCollapse = CombatNames::modelCondition("POST_COLLAPSE");
	t->setModelConditionState(kPostRubble, true);
	w.frames(3);
	CHECK(r->state() == RubbleRiseUpdate::WAITING); // asleep: no death yet
	DieModuleInterface::Event death{};
	r->onDie(death);
	// the test world has no terrain: the rubble's height is the object's own, so the first rising step (the same update as the start) ends the rise
	w.frames(1);
	CHECK(r->state() == RubbleRiseUpdate::DONE);
	CHECK(t->getPosition()->z == r->rubbleHeight());
	CHECK_FALSE(t->testModelCondition(kPostRubble));
	CHECK(t->testModelCondition(kPostCollapse));
}

TEST_CASE("castlemod1: OCLUpdate sets its timer on the first due frame, then fires every MinDelay .. MaxDelay frames until Amount (RW 0x89EC43)")
{
	CombatWorld w(kGates);
	Object *t = w.unit("ProbeTimer", 'A', 300, 300);
	OCLUpdate *u = dynamic_cast<OCLUpdate *>(t->findModule("OCLUpdate"));
	REQUIRE(u);
	w.frames(1);
	const UnsignedInt first = u->nextCreationFrame();
	CHECK(first != 0u); // tolerance: set on the frame the update first ran; the steps below are exact
	CHECK(u->made() == 0);
	w.frames(6);
	CHECK(u->made() == 1);
	CHECK(u->nextCreationFrame() == first + 5);
	w.frames(6);
	CHECK(u->made() == 2);
	w.frames(12);
	CHECK(u->made() == 2); // Amount 2: asleep for good
}

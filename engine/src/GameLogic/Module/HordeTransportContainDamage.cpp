// OpenBFME. GPL-3.0.
// See GameLogic/Module/HordeTransportContainDamage.h (fork HORDEDMG-1: RotWK 2.01 RW 0x8BF279 .. 0x8BF4D7).

#include "GameLogic/Module/HordeTransportContainDamage.h"

#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstdint>
#include <vector>

void HordeTransportContainDamage::registerModule(ModuleFactory &modules)
{
	// RW 0x65AE15: the data stays the binary's plain ModuleData (RW 0x650C72, no field)
	modules.bindModuleProc("HordeTransportContainDamage", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		return std::make_unique<HordeTransportContainDamage>(thing, data);
	});
}

// RW 0x8BF2BF
void HordeTransportContainDamage::onDamage(const DamageInfo &info)
{
	Object *me = getObject();
	const BodyModuleInterface *body = me->getBodyModule(); // + 0x25C
	if (!body)
	{
		return;
	}
	const float health = body->getHealth();       // slot 0x10
	const float maxHealth = body->getMaxHealth(); // slot 0x1C
	const float sum = SimMath::addf32(info.m_output.m_actualDamageClipped, health); // + 0x74
	const float before = maxHealth > sum ? sum : maxHealth;                         // comiss / ja
	ContainModuleInterface *contain = me->getContain(); // + 0x258
	if (!contain)
	{
		return;
	}
	static const unsigned kHorde = (unsigned)CombatNames::kindOf("HORDE"); // template + 0x115 bit 5
	std::vector<ObjectID> doomed; // RW 0x5FFE1E: the ids to kill, collected over every passenger first
	for (Object *passenger : *contain->getContainedItemsList())
	{
		if (!passenger || !passenger->isKindOf(kHorde))
		{
			continue;
		}
		ContainModuleInterface *hordeContain = passenger->getContain();
		HordeContainInterface *horde = hordeContain ? hordeContain->getHordeContainInterface() : nullptr; // contain slot 0x7C, horde slot 0x218
		if (!horde)
		{
			continue;
		}
		const unsigned count = horde->countWithEntering(); // slot 0x180(0)
		if (count <= 1)
		{
			continue;
		}
		const unsigned slots = (unsigned)horde->getSlotCapacity(); // slot 0x17C
		if (slots == 0)
		{
			continue;
		}
		// x87 PC24, RW 0x8BF3E7 .. 0x8BF416: fild C; fdivr M; fld1; fdivrp (1 / (M / C), kept wide); fld P; fmul; _ftol2; fmul H; _ftol2
		const double perHealth = SimMath::pc24DivW(1.0, SimMath::pc24DivW((double)maxHealth, (double)slots));
		std::uint32_t pre = SimMath::ftol2Low32(SimMath::pc24MulW((double)before, perHealth)) + 1;
		pre = pre >= slots ? slots : pre;
		std::uint32_t post = SimMath::ftol2Low32(SimMath::pc24MulW(perHealth, (double)health)) + 1;
		post = post >= slots ? slots : post;
		if (count <= post)
		{
			continue;
		}
		std::uint32_t kills = pre - post;
		if (count - kills < post) // unsigned, RW 0x8BF421 .. 0x8BF42B
		{
			kills = count - post;
		}
		std::uint32_t i = 0;
		for (Object *member : *hordeContain->getContainedItemsList()) // slot 0x118 of the horde contain (+ 0x20)
		{
			if (i >= kills)
			{
				break;
			}
			if (member)
			{
				doomed.push_back(member->getID()); // + 0x74
			}
			++i; // an empty node counts as one (RW 0x8BF47C)
		}
	}
	for (ObjectID id : doomed)
	{
		if (Object *o = me->logic().findObjectByID(id)) // RW 0x449681
		{
			o->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL); // RW 0x698EC3(8, 0)
		}
	}
}

// RW 0x8C651D: DamageModule's: version 1, then RW 0x9933FE
void HordeTransportContainDamage::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
}

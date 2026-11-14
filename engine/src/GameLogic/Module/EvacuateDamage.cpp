// OpenBFME. GPL-3.0.
// See GameLogic/Module/EvacuateDamage.h (fork lane EVACUATE-1: RotWK 2.01 RW 0x8BEDF8 .. 0x8BF22E).

#include "GameLogic/Module/EvacuateDamage.h"

#include "Common/INI.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <cstddef>
#include <stdexcept>

namespace
{
using Data = EvacuateDamageModuleData;

// RW 0x8BEDF8: the name is kept only when TheWeaponStore knows the template (RW 0x6CC5DF); otherwise the field stays as it was (empty by default)
void parseEvacuationWeapon(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (TheWeaponStore && TheWeaponStore->findWeaponTemplate(name))
	{
		*static_cast<std::string *>(store) = name;
	}
}

// RW 0xC70A88
const FieldParse kFields[] = {
	{ "WeaponThatCausesEvacuation", parseEvacuationWeapon, nullptr, (int)offsetof(Data, m_weaponThatCausesEvacuation) },
	{ "DamageTypeToTrack", INI::parseIndexList, TheDamageNames, (int)offsetof(Data, m_damageTypeToTrack) },
	{ "DamageToPanicThreshold", INI::parsePercentToReal, nullptr, (int)offsetof(Data, m_damageToPanicThreshold) },
	{ "TrackingTimeSpan", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_trackingTimeSpan) },
	{ nullptr, nullptr, nullptr, 0 },
};

const char *const kFleeStop = "[EVACUATE-1] EvacuateDamage's panic (RW 0x8BF076): the records were cleared as retail, but aiFleeFromObject (RW 0x881FF3, AI "
	"command 0x2E) has no port: the object flees nowhere";
} // namespace

void EvacuateDamageModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

const char *EvacuateDamage::stopLine()
{
	return kFleeStop;
}

void EvacuateDamage::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "EvacuateDamage"; // RW 0x65ADE0
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<EvacuateDamage>(thing, typed);
	});
}

// RW 0x8BF12E
void EvacuateDamage::onDamage(const DamageInfo &info)
{
	Object *me = getObject();
	Object *source = me->logic().findObjectByID(info.m_input.m_sourceID); // RW 0x8BF139 .. 0x8BF147
	const ObjectWeapons *weapons = source ? source->getWeapons() : nullptr;
	const Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr; // RW 0x68B58C(0)
	if (!weapon)
	{
		return;
	}
	const WeaponTemplate *t = weapon->getTemplate();
	if (t && t->getName() == m_data->m_weaponThatCausesEvacuation) // RW 0x8BF165 .. 0x8BF176: the template's name (+ 4 -> + 8), AsciiString compare
	{
		evacuate();
		return;
	}
	if (m_data->m_damageTypeToTrack == info.m_input.m_damageType) // RW 0x8BF1EC .. 0x8BF1F2
	{
		track(info);
	}
}

// RW 0x8BF178 .. 0x8BF1EA
void EvacuateDamage::evacuate()
{
	Object *me = getObject();
	ContainModuleInterface *contain = me->getContain();
	if (!contain)
	{
		return;
	}
	// contain slot 0x74 (the exit interface) answers for every ported contain (EvacuateDamage.h): every passenger of the list (slot 0x118) in order
	static const int kHorde = CombatNames::kindOf("HORDE"); // template + 0x115 bit 5
	std::vector<ObjectID> riders;
	for (Object *o : *contain->getContainedItemsList())
	{
		riders.push_back(o->getID());
	}
	for (ObjectID id : riders)
	{
		Object *rider = me->logic().findObjectByID(id);
		AIUpdateInterface *ai = rider ? rider->getAIUpdateInterface() : nullptr;
		if (!ai)
		{
			continue; // retail calls through the rider's AI (+ 0x260) without a test: no retail data puts a rider without one in these contains
		}
		if (rider->isKindOf((unsigned)kHorde))
		{
			ai->aiHordeExit(me, CMD_FROM_AI); // RW 0x775AFB
		}
		else
		{
			ai->aiExit(me, CMD_FROM_AI); // RW 0x7716C1
		}
	}
}

// RW 0x8BEF0A: the cutoff is now - TrackingTimeSpan; a record at or before it is erased, a later one counts (SSE addss in list order)
float EvacuateDamage::recentDamage()
{
	const unsigned now = getObject()->logic().getFrame();
	const unsigned cutoff = now - m_data->m_trackingTimeSpan;
	float sum = 0.0f;
	for (auto it = m_records.begin(); it != m_records.end();)
	{
		if ((int)it->first <= (int)cutoff) // RW 0x8BEF33: cmp [record + 0xC], esi; jg keeps (signed)
		{
			it = m_records.erase(it); // RW 0x702055
		}
		else
		{
			sum = SimMath::addf32(it->second, sum);
			++it;
		}
	}
	return sum;
}

// RW 0x8BF09E
void EvacuateDamage::track(const DamageInfo &info)
{
	Object *me = getObject();
	BodyModuleInterface *body = me->getBodyModule();
	if (!body)
	{
		return; // RW 0x8BF0BD: no body (+ 0x25C)
	}
	m_records.emplace_back(me->logic().getFrame(), info.m_input.m_amount); // RW 0x777F5C: { amount D + 0x20, now }
	const float maxHealth = body->getMaxHealth();                           // body vslot 0x1C
	const float sum = recentDamage();
	// RW 0x8BF0FF .. 0x8BF10B: x87 fld maxHealth; fmul threshold; fcompi with the sum: below the product, nothing (one rounding of the product: the SSE product)
	if (sum < SimMath::mulf32(maxHealth, m_data->m_damageToPanicThreshold))
	{
		return;
	}
	if (Object *source = me->logic().findObjectByID(info.m_input.m_sourceID))
	{
		panic(*source);
	}
}

// RW 0x8BF076
void EvacuateDamage::panic(Object &source)
{
	(void)source;
	Object *me = getObject();
	if (!me->getAIUpdateInterface())
	{
		return;
	}
	m_records.clear(); // RW 0x862535
	me->logic().noteStop(kFleeStop); // RW 0x881FF3(source, CMD_FROM_AI): not ported (EvacuateDamage.h)
}

void EvacuateDamage::crc(StateHasher &hasher) const
{
	BehaviorModule::crc(hasher);
	if (!m_records.empty()) // hashed while it tracks (an object never hit by the tracked type keeps its hash)
	{
		hasher.addU32(0xE7AC0000u | (std::uint32_t)m_records.size());
		for (const auto &r : m_records)
		{
			hasher.addU32(r.first);
			hasher.addFloat(r.second);
		}
	}
}

void EvacuateDamage::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
	std::vector<std::pair<unsigned, float>> &records = m_records;
	x.xferVectorSize(records, 8);
	for (auto &r : records)
	{
		x.xferUnsignedInt(&r.first);
		x.xferReal(&r.second);
	}
}

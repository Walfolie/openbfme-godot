// OpenBFME. GPL-3.0.
// See GameLogic/Module/DamageFieldUpdate.h (fork lane DAMAGEFIELD-1: RotWK 2.01 RW 0x894773 .. 0x894C3E).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/DamageFieldUpdate.h"

#include "Common/INI.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace
{
using Data = DamageFieldUpdateModuleData;

// RW 0xC64018
const FieldParse kFields[] = {
	{ "Radius", INI::parseInt, nullptr, (int)offsetof(Data, m_radius) },
	{ "ObjectFilter", ParseObjectFilter, nullptr, (int)offsetof(Data, m_objectFilter) },
	{ "RequiredUpgrade", INI::parseAsciiString, nullptr, (int)offsetof(Data, m_requiredUpgrade) },
	{ nullptr, nullptr, nullptr, 0 },
};
} // namespace

// RW 0x894773: FireWeaponUpdate's table (RW 0x88F720), then RW 0xC64018
void DamageFieldUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	FireWeaponUpdateModuleData::buildFieldParse(p);
	p.add(kFields);
}

void DamageFieldUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "DamageFieldUpdate"; // RW 0x659082
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<DamageFieldUpdate>(thing, typed);
	});
}

DamageFieldUpdate::DamageFieldUpdate(Thing *thing, const Data *data)
	: FireWeaponUpdate(thing, data)
	, m_field(data)
{
}

// RW 0x8949A2
UpdateSleepTime DamageFieldUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (!m_field->m_requiredUpgrade.empty()) // RW 0x8949CD: the name against "" (RW 0xDC62B8)
	{
		const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(m_field->m_requiredUpgrade) : nullptr; // RW 0x66F5E5
		if (u && !obj->hasUpgrade(u))                                                                                    // RW 0x691421
		{
			return UPDATE_SLEEP(1);
		}
	}
	const Player *player = obj->getControllingPlayer(); // RW 0x68B678
	PartitionFilterFn byFilter([&](Object &o) { return ObjectFilterMatch::allows(logic, m_field->m_objectFilter, o, player); }); // RW 0x66122D (flag 1)
	PartitionFilterFn sameMap([obj](Object &o) { return o.isOffMap() == obj->isOffMap(); });                                      // RW 0x660E93
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });                                                    // RW 0xC10E20
	PartitionFilterFn enemies([obj](Object &o) { return obj->getRelationship(o) == ENEMIES; });                                   // RW 0x660B85, mask 1
	// RW 0x894A61 .. 0x894A93: the chain is linked enemies -> alive -> same map -> filter; every link is a pure test, so the order changes no answer
	const PartitionHits hits = logic.partition().iterateObjectsInRange(*obj->getPosition(), (float)m_field->m_radius, FROM_CENTER_2D,
		{ &enemies, &alive, &sameMap, &byFilter }, ITER_FASTEST); // cvtsi2ss Radius
	std::set<ObjectID> seen; // RW ebp - 0x18
	for (const PartitionHit &h : hits)
	{
		const ObjectID id = h.object->getID(); // + 0x74
		seen.insert(id);
		if (m_inField.find(id) == m_inField.end())
		{
			fireAtVictim(h.object); // RW 0x89483A
			m_inField.insert(id);   // RW 0x6A253A
		}
	}
	std::vector<ObjectID> gone; // RW ebp - 0x14
	for (ObjectID id : m_inField)
	{
		if (seen.find(id) == seen.end())
		{
			gone.push_back(id);
		}
	}
	for (ObjectID id : gone)
	{
		m_inField.erase(id); // RW 0x6AC9F9
	}
	return nextSleep(); // RW 0x88F0AA
}

void DamageFieldUpdate::crc(StateHasher &hasher) const
{
	FireWeaponUpdate::crc(hasher);
	if (!m_inField.empty()) // hashed only while the field holds an object
	{
		hasher.addU32(0xDF100000u | (std::uint32_t)m_inField.size());
		for (ObjectID id : m_inField)
		{
			hasher.addU32(id);
		}
	}
}

// RW 0x894C09: version 1, the set (RW 0x73385A), FireWeaponUpdate's (RW 0x88F113)
void DamageFieldUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	std::uint32_t n = (std::uint32_t)m_inField.size();
	x.xferUnsignedInt(&n);
	if (x.isLoading())
	{
		m_inField.clear();
		for (std::uint32_t i = 0; i < n; ++i)
		{
			ObjectID id = INVALID_ID;
			x.xferObjectID(&id);
			m_inField.insert(id);
		}
	}
	else
	{
		for (ObjectID id : m_inField)
		{
			ObjectID v = id;
			x.xferObjectID(&v);
		}
	}
	FireWeaponUpdate::xfer(x);
}

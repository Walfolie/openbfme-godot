// OpenBFME. GPL-3.0.
// See GameLogic/Module/SpellRechargeModifierUpgrade.h (fork lane SPELLRECHARGE-1: RotWK 2.01 RW 0x8B9F57 .. 0x8BA2B5).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SpellRechargeModifierUpgrade.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>

namespace
{
using Data = SpellRechargeModifierUpgradeModuleData;

// RW 0x8BA26F: parsePercentToReal, appended to the vector
void parsePercentageAppend(INI *ini, void *, void *store, const void *)
{
	float value = 0.0f;
	INI::parsePercentToReal(ini, nullptr, &value, nullptr);
	static_cast<std::vector<float> *>(store)->push_back(value);
}

// RW 0xC6F0D0
const FieldParse kFields[] = {
	{ "Percentage", parsePercentageAppend, nullptr, (int)offsetof(Data, m_percentage) },
	{ "StartsActive", INI::parseBool, nullptr, (int)offsetof(Data, m_startsActive) },
	{ "LabelForPalantirString", INI::parseAsciiString, nullptr, (int)offsetof(Data, m_labelForPalantirString) },
	{ nullptr, nullptr, nullptr, 0 },
};
} // namespace

void SpellRechargeModifierUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kFields);
}

void SpellRechargeModifierUpgrade::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "SpellRechargeModifierUpgrade"; // RW 0x65A21A
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<SpellRechargeModifierUpgrade>(thing, typed);
	});
}

SpellRechargeModifierUpgrade::SpellRechargeModifierUpgrade(Thing *thing, const Data *data)
	: UpgradeModule(thing, data)
	, m_data(data)
{
}

// RW 0x8BA15D .. 0x8BA1A2 (and its copies in 0x8B9FE0 / 0x8BA05E): i = count - 1, at least 0, below size - 1 else size - 1
void SpellRechargeModifierUpgrade::applyModifier(Player &player) const
{
	const int size = (int)m_data->m_percentage.size();
	if (size == 0)
	{
		return; // INFERENCE (header): retail reads [-1]
	}
	int i = player.science().rechargeModifierCount() - 1;
	if (!(i > 0))
	{
		i = 0;
	}
	if (!(i < size - 1))
	{
		i = size - 1;
	}
	player.science().setRechargeDiscount(m_data->m_percentage[(size_t)i]); // RW 0x6AAAEE
}

// RW 0x8BA139
void SpellRechargeModifierUpgrade::upgradeImplementation()
{
	Player *owner = getObject()->getControllingPlayer(); // RW 0x68B678 (not tested in retail)
	if (!owner)
	{
		return;
	}
	owner->science().incRechargeModifierCount(); // RW 0x6AAAE0
	applyModifier(*owner);
}

// RW 0x8B9FE0
void SpellRechargeModifierUpgrade::processUpgradeRemoval()
{
	if (!isAlreadyUpgraded()) // mux slot 0
	{
		return;
	}
	if (Player *owner = getObject()->getControllingPlayer())
	{
		owner->science().decRechargeModifierCount(); // RW 0x6AAAE7
		applyModifier(*owner);
	}
}

// RW 0x8B9FB6
void SpellRechargeModifierUpgrade::onBuildComplete()
{
	if (m_data->m_startsActive) // data + 0x144
	{
		upgradeImplementation(); // mux slot 10
		setUpgradeExecuted(true); // mux slot 9
	}
}

// RW 0x8BA05E
void SpellRechargeModifierUpgrade::onCapture(Player *oldOwner, Player *newOwner)
{
	if (oldOwner)
	{
		oldOwner->science().decRechargeModifierCount();
		applyModifier(*oldOwner);
	}
	if (newOwner)
	{
		newOwner->science().incRechargeModifierCount();
		applyModifier(*newOwner);
	}
	setUpgradeExecuted(true); // RW 0x8BA128: mux slot 9 (1)
}

void SpellRechargeModifierUpgrade::crc(StateHasher &hasher) const
{
	if (isAlreadyUpgraded())
	{
		hasher.addU32(0x5E11C001u);
	}
}

void SpellRechargeModifierUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UpgradeModule::xfer(x);
}

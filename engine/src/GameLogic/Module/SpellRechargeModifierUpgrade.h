// OpenBFME. GPL-3.0.
//
// SpellRechargeModifierUpgrade (fork lane SPELLRECHARGE-1): the signal fire's spell book discount. RotWK 2.01 data: one template, SignalFire
// (data/ini/object/neutral/signalfire.ini: StartsActive, Percentage -25% then -50%): the owner's spell book recharges 25% faster with one signal fire, 50%
// with two or more. Read from RotWK's game.dat (md5 daed3668, caveat S-001):
//   * registration RW 0x65A21A ("SpellRechargeModifierUpgrade" RW 0xC0AFC4), the module's create RW 0x650104 (size 0x24), the data's RW 0x65013C (0x14C);
//   * the data (RW 0x8BA1AB after the upgrade data RW 0x6531CE; table RW 0xC6F0D0 after the upgrade table RW 0x8D26E0(8)): Percentage (+ 0x138, RW
//     0x8BA26F: parsePercentToReal appended to a vector), StartsActive (+ 0x144, parseBool, false), LabelForPalantirString (+ 0x148, ">UNSPECIFIED<" RW
//     0xC6EE6C);
//   * the module (constructor RW 0x8B9F57 over UpgradeModule RW 0x863B8F; vtables RW 0xC6F020, the mux RW 0xC6EFD8 at + 0x10, the create interface RW
//     0xC6EFC8 at + 0x1C); "the modifier" below = the controlling player's recharge discount (Player + 0x718, RW 0x6AAAEE) set to Percentage[i], i = the
//     player's modifier count (Player + 0x71C) - 1 clamped to [0, size - 1]:
//       - upgradeImplementation (mux slot 10, RW 0x8BA139): the count + 1 (RW 0x6AAAE0), the modifier (the player is not tested);
//       - the removal (mux slot 8, RW 0x8B9FE0): when executed and with a controlling player, the count - 1 (RW 0x6AAAE7), the modifier (the executed flag
//         is left: UpgradeMux::removeUpgrade clears it); the module's slot 8 (RW 0x8B9B3C, onDelete) is this removal;
//       - onCapture (slot 9, RW 0x8BA05E): an old owner's count - 1 and its modifier, a new owner's count + 1 and its modifier (neither tests the executed
//         flag), then setUpgradeExecuted(true);
//       - onBuildComplete (create slot 1, RW 0x8B9FB6): with StartsActive the implementation (mux slot 10) and setUpgradeExecuted(true).
// Retail behaviour kept: the last fire lost leaves the count 0, which clamps to index 0: the discount stays at Percentage[0] (it is never reset).
// INFERENCE: an empty Percentage list (no retail data) would index [-1] in retail; the port leaves the modifier unchanged.

#pragma once

#include "GameLogic/Module/UpgradeModule.h"

#include <string>
#include <vector>

class ModuleFactory;
class Player;

class SpellRechargeModifierUpgradeModuleData : public UpgradeModuleData
{
public:
	std::vector<float> m_percentage;                        ///< + 0x138
	bool m_startsActive = false;                            ///< + 0x144
	std::string m_labelForPalantirString = ">UNSPECIFIED<"; ///< + 0x148
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SpellRechargeModifierUpgrade : public UpgradeModule, public CreateModuleInterface
{
public:
	OBFME_MODULE_XFER(SpellRechargeModifierUpgrade) // UpgradeModule's (the module keeps no state of its own)
	SpellRechargeModifierUpgrade(Thing *thing, const SpellRechargeModifierUpgradeModuleData *data); // RW 0x8B9F57
	CreateModuleInterface *getCreate() override { return this; }
	void onDelete() override { processUpgradeRemoval(); } // slot 8 (RW 0x8B9B3C)
	void onCreate() override {}
	void onBuildComplete() override;                                  // RW 0x8B9FB6
	void onCapture(Player *oldOwner, Player *newOwner) override;      // RW 0x8BA05E
	void crc(StateHasher &hasher) const override; // the executed flag only when set (an inactive fire hashes as the unported module did)
	static void registerModule(ModuleFactory &modules);

protected:
	void upgradeImplementation() override; // RW 0x8BA139
	void processUpgradeRemoval() override; // RW 0x8B9FE0

private:
	void applyModifier(Player &player) const; // Percentage[clamp(count - 1)] -> Player + 0x718
	const SpellRechargeModifierUpgradeModuleData *m_data;
};

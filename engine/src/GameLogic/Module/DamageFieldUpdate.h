// OpenBFME. GPL-3.0.
//
// DamageFieldUpdate (fork lane DAMAGEFIELD-1): a FireWeaponUpdate that shoots at each object entering its radius. RotWK 2.01 data: one use, the Wild
// citadel's Razor Spines (WildFortressCitadel ModuleTag_RazorSpinesWeapon: Radius 100, ObjectFilter ALL ENEMIES, RequiredUpgrade
// Upgrade_WildFortressRazorSpines, one FireWeaponNugget RazorSpinesBasicWeapon). Read from RotWK's game.dat (md5 daed3668, caveat S-001):
//   * registration RW 0x659082 ("DamageFieldUpdate" RW 0xC0B670, interface mask 1: update), the module's create RW 0x64D461 (0x30 bytes), the data's RW
//     0x64D499 (0x1C bytes);
//   * the data (RW 0x89478E over FireWeaponUpdate's RW 0x88F26F; table RW 0xC64018 after FireWeaponUpdate's RW 0xC62A40, RW 0x894773): Radius (+ 0x10,
//     parseInt RW 0x42EC5E, 0), ObjectFilter (+ 0x14, RW 0x76392F, the constructor's default RW 0x76406F), RequiredUpgrade (+ 0x18, a name, "");
//   * the module (constructor RW 0x894935 over FireWeaponUpdate's RW 0x88F731; vtables RW 0xC640E0 / 0xC640D4): + 0x2C the ids of the objects in the field
//     (an ordered set, RW 0x6A057C); xfer RW 0x894C09: version 1, the set (RW 0x73385A), FireWeaponUpdate's (RW 0x88F113);
//   * update RW 0x8949A2: with a RequiredUpgrade that TheUpgradeCenter knows (RW 0x66F5E5) and the object does not have (RW 0x691421): sleep 1 frame. Else
//     the objects within (float)Radius of the object's position (RW 0xA39340: FROM_CENTER_2D, ITER_FASTEST) that pass the ObjectFilter for the controlling
//     player (RW 0x66122D), have the object's OFF_MAP bit (Object + 0x458 bit 3, RW 0x660E93), are alive (RW 0xC10E20) and are the object's enemies (RW
//     0x660B85, mask 1), in iteration order: each one not yet in the set is shot at (RW 0x89483A, FireWeaponUpdate::fireAtVictim) and added; then every id
//     of the set not found this time is removed. Returns FireWeaponUpdate's sleep (RW 0x88F0AA).
// A victim is shot once when it enters (a weapon that is not ready then does not shoot it later while it stays).

#pragma once

#include "GameLogic/ObjectFilter.h"
#include "GameLogic/Module/SpellEffectModules.h"

#include <set>
#include <string>

class ModuleFactory;

class DamageFieldUpdateModuleData : public FireWeaponUpdateModuleData
{
public:
	int m_radius = 0;               ///< + 0x10
	ObjectFilter m_objectFilter;    ///< + 0x14 (RW 0x76406F)
	std::string m_requiredUpgrade;  ///< + 0x18
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DamageFieldUpdate : public FireWeaponUpdate
{
public:
	OBFME_MODULE_XFER(DamageFieldUpdate)
	DamageFieldUpdate(Thing *thing, const DamageFieldUpdateModuleData *data); // RW 0x894935
	UpdateSleepTime update() override;                                        // RW 0x8949A2
	void crc(StateHasher &hasher) const override;
	static void registerModule(ModuleFactory &modules);

	const std::set<ObjectID> &inField() const { return m_inField; } // + 0x2C

private:
	const DamageFieldUpdateModuleData *m_field;
	std::set<ObjectID> m_inField;
};

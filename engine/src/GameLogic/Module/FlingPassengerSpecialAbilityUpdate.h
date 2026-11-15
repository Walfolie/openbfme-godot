// OpenBFME. GPL-3.0.
//
// FlingPassengerSpecialAbilityUpdate (fork lane FLING-1): the ability that lets go of what a one-slot TransportContain holds. RotWK 2.01 data: 3 templates,
// MordorMountainTroll (SpecialAbilityTrollDropTree: the tree), IsengardSharku (SpecialAbilityIsengardSharkuManEaterDrop) and AngmarShadeWolf
// (SpecialAbilityAngmarShadeEatMeDrop): the eaten victim, killed by FlingPassengerLandingWarhead. Read from RotWK's game.dat (md5 daed3668, caveat S-001):
//   * registration RW 0x65981E ("FlingPassengerSpecialAbilityUpdate", RW 0xC0B37C), the module's constructor RW 0x898672 over SpecialAbilityUpdate's
//     RW 0x851D42, vtable RW 0xC652D8: only slot 0x44 (triggerAbilityEffect) is overridden (RW 0x89875A);
//   * the data (RW 0x89870E, field table RW 0xC0556C after SpecialAbilityUpdate's): FlingPassengerVelocity (+ 0xD0, Coord3D; no reader found in the
//     binary: every retail user writes X:0 Y:0 Z:0) and FlingPassengerLandingWarhead (+ 0xDC, a WeaponTemplate pointer, null for an unknown name);
//   * triggerAbilityEffect RW 0x89875A: the base's (RW 0x853EDF); then, when the contain (+ 0x258, read unchecked) holds anything (slot 0x114(0) = OpenContain
//     RW 0x865CE3: the count + 0x38), the first rider of the list (slot 0x13C(0) = RW 0x866367, no filter) is taken out (its own slot 0xA4: removeFromContain,
//     TransportContain RW 0x87A6D3); with a warhead TheWeaponStore fires it from the object at the rider (RW 0x6CF590); a rider now effectively dead (+ 0x458
//     bit 0) gets the model conditions DYING (+ 0x110 bit 30 = 62) and SPLATTED (+ 0x118 bit 26 = 122), each pushed to the drawable (RW 0x68B53C) when it
//     was clear; a living one with physics (+ 0x264) stands up (RW 0x792AFF).
// INFERENCE: the port tests the contain and the rider for null (retail data gives every user a TransportContain, and a count above 0 has a first rider).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/Module/SpecialAbilityModules.h"

class ModuleFactory;
class MultiIniFieldParse;
class WeaponTemplate;

class FlingPassengerSpecialAbilityUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	Coord3D m_flingPassengerVelocity{};                     ///< + 0xD0 (parsed; read nowhere in 2.01)
	const WeaponTemplate *m_flingPassengerLandingWarhead = nullptr; ///< + 0xDC
	static void buildFieldParse(MultiIniFieldParse &p);
};

class FlingPassengerSpecialAbilityUpdate : public SpecialAbilityUpdate
{
public:
	OBFME_MODULE_XFER(FlingPassengerSpecialAbilityUpdate) // SpecialAbilityUpdate's state only (the module keeps none of its own)
	FlingPassengerSpecialAbilityUpdate(Thing *thing, const FlingPassengerSpecialAbilityUpdateModuleData *data); // RW 0x898672
	static void registerModule(ModuleFactory &modules);
	const FlingPassengerSpecialAbilityUpdateModuleData *flingData() const { return m_fling; }
	void triggerForTest() { triggerAbilityEffect(); } // tests only: the effect without the ability's approach and preparation

protected:
	void triggerAbilityEffect() override; // RW 0x89875A

private:
	const FlingPassengerSpecialAbilityUpdateModuleData *m_fling;
};

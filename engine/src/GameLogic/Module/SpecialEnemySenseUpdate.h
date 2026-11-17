// OpenBFME. GPL-3.0.
//
// SpecialEnemySenseUpdate (fork SENSE-1, package B of docs/fork/MODULE_CENSUS.md): Frodo's Sting glowing near orcs (RotWK 2.01 data: frodo.ini
// ModuleTag_StingSeesOrcs, SpecialEnemyFilter = ANY +ORC +URUK +MordorShelob, ScanRange 200, ScanInterval 2000; and one cinematic object). A BFME
// class; ZH has none. Donor: the BFME2 decomp's SpecialEnemySenseUpdate.cpp (update 0x0049A037); read again from RotWK's game.dat (md5 daed3668,
// caveat S-001), the same shape:
//   * registration RW 0x658E8A ("SpecialEnemySenseUpdate", RTTI name RW 0xC0B718); the data RW 0x653E58 (0x14 bytes, ctor RW 0x653E06: the
//     ObjectFilter at + 8 (RW 0x76406F), ScanRange + 0xC = 0, ScanInterval + 0x10 = 1; field table RW 0xC07E98: SpecialEnemyFilter (RW 0x76392F),
//     ScanRange (real, RW 0x42ED00), ScanInterval (duration, RW 0x73A429)); the module RW 0x64E165 (0x20 bytes, ctor RW 0x89D758: UpdateModule's,
//     vtables RW 0xC66C10 / 0xC67300 / 0xC66C04, wakes on the next frame (RW 0x850C32 with 1)).
//   * update RW 0x89D7E6: the closest object (RW 0xA39090, FROM_CENTER_2D) to the object's position within ScanRange through the chain not
//     effectively dead (RW 0xC10E20) -> SpecialEnemyFilter for the controlling player (RW 0xBE4CC8, RW 0x68B678) -> an ENEMY of the object
//     (RW 0xC11DC0, flags 1, direction 0). Found: model condition 258 (SPECIAL_ENEMY_NEAR, + 0x12C bit 2) set, none: cleared, each only on a change
//     (then RW 0x68B53C); on every member of the contain's horde interface list (contain slot 0x7C, then its slot 0x108: INFERENCE, as in
//     WeaponDelivery, the horde contain's own list) when there is one, else on the object itself. Sleeps ScanInterval (+ 0x10).
//   * xfer RW 0x89D7C5: UpdateModule's (RW 0x850C60), then (not a light CRC) version 1 (RW 0x404F15): no field of its own.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"

class ModuleFactory;

class SpecialEnemySenseUpdateModuleData : public ModuleData
{
public:
	ObjectFilter m_specialEnemyFilter; ///< + 8
	float m_scanRange = 0.0f;          ///< + 0xC
	unsigned m_scanInterval = 1;       ///< + 0x10 (frames)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SpecialEnemySenseUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(SpecialEnemySenseUpdate)
	SpecialEnemySenseUpdate(Thing *thing, const SpecialEnemySenseUpdateModuleData *data); // RW 0x89D758
	UpdateSleepTime update() override;                                                   // RW 0x89D7E6
	static void registerModule(ModuleFactory &modules);

private:
	const SpecialEnemySenseUpdateModuleData *m_data;
};

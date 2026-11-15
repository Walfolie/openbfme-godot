// OpenBFME. GPL-3.0.
// S-207: SpawnUnitBehavior's template offer, recovered from the supplied
// modified RotWK image (S-001), not independently verified clean-retail parity.
// RW 0x6506C0 -> 0x8BC924 installs behavior vtable 0xC6FDE8;
// slot 0x2C returns the +0x20 interface, whose getter RW 0x8BC992
// copies ModuleData +0x08 (UnitName). The query has no runtime-state gates.
//
// Fork lane SPAWNUNIT-1: the update (RotWK 2.01 game.dat md5 daed3668, the update interface at module + 0x10, RW 0x8BC9B0). The constructor (RW 0x8BC924
// over UpdateModule RW 0x653114) copies SpawnOnce into + 0x24 ("may still spawn"). Every update returns 5 frames (RW 0xD9F608, LOGICFRAMES_PER_SECOND) and
// does nothing without a controlling player, before frame 5, when the player's auto build flag (Player + 0x755: 1 at init RW 0x6B04E9, written only by
// the script action TOGGLE_AUTO_BUILD RW 0x7BDBAB, not ported: always on here) is off, or while the object is AWAITING_CONSTRUCTION (67) or
// ACTIVELY_BEING_CONSTRUCTED (69). Then:
//   * with a ProductionUpdate (RW 0x68C327) holding two or more entries: when the first one's template (+ 8) is named UnitName (template + 0x64), that
//     entry is cancelled (slot 10 cancelUnitCreate(+ 0x10), refunded): the bought creature does not hold the citadel's queue;
//   * with one entry: nothing;
//   * with none (or no ProductionUpdate): UnitName's template (RW 0x6D1305; none: nothing) that the player's command points afford (RW 0x6A7F79), the
//     UnitCommand button (TheControlBar RW 0x71D6EA); a SpawnOnce module that already spawned stops here; else the button (when found) runs on the object
//     (RW 0x696FD2(button, CMD_FROM_AI, 0)) and + 0x24 = 0.
// 2.01 data: the Elven and the Wild citadel name no UnitCommand (the button lookup finds nothing); GondorArmyofTheDeadEgg's
// Command_ConstructRohanOathbreakerHorde is commented out of _patch201ini.big's commandbutton.ini, so its lookup finds nothing either.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

class SpawnUnitBehaviorModuleData : public ModuleData
{
public:
	std::string m_unitName;    ///< RW data +0x08, default empty
	std::string m_unitCommand; ///< RW data +0x0C, default empty
	bool m_spawnOnce = false; ///< RW data +0x1C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SpawnUnitBehavior : public UpdateModule, public SpawnUnitBehaviorInterface
{
public:
	SpawnUnitBehavior(Thing *thing, const SpawnUnitBehaviorModuleData *data); // RW 0x8BC924
	OBFME_MODULE_XFER(SpawnUnitBehavior)
	SpawnUnitBehaviorInterface *getSpawnUnitBehaviorInterface() override { return this; }
	const std::string &getUnitName() const override { return m_data->m_unitName; }
	UpdateSleepTime update() override; // RW 0x8BC9B0
	void crc(StateHasher &hasher) const override;

	bool maySpawn() const { return m_maySpawn; }                   // + 0x24
	unsigned long long cancels() const { return m_cancels; }       // queue entries cancelled (OpenBFME counter, hashed when non-zero)

private:
	const SpawnUnitBehaviorModuleData *m_data;
	bool m_maySpawn;                  ///< + 0x24 (SpawnOnce at construction)
	unsigned long long m_cancels = 0;
};

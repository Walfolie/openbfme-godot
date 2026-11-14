// OpenBFME. GPL-3.0.
//
// EvacuateDamage (fork lane EVACUATE-1): the damage module of the battle towers, keeps and garrisonable castle parts (RotWK 2.01 data: 19 templates,
// GondorKeep, IsengardBattleTower, ElvenBattleTower, the castle base keeps, the ruined towers ...). Read from RotWK's game.dat (md5 daed3668, caveat S-001):
//   * registration RW 0x65ADE0 ("EvacuateDamage", MODULETYPE_BEHAVIOR, interface mask 4: damage), the data's constructor RW 0x8BEE59, the module's RW 0x8BEF66;
//   * the data (RW 0x8BEE59, field table RW 0xC70A88): WeaponThatCausesEvacuation (+ 0x08, RW 0x8BEDF8: the name is kept only when TheWeaponStore knows the
//     template, RW 0x6CC5DF; else it stays empty), DamageTypeToTrack (+ 0x0C, parseIndexList over the damage names, default 3 PIERCE), DamageToPanicThreshold
//     (+ 0x10, parsePercentToReal, default 0.1 RW 0xBD83D4), TrackingTimeSpan (+ 0x14, parseDurationUnsignedInt, default 6 frames);
//   * onDamage RW 0x8BF12E: no source object (RW 0x449681 of the damage's source id) or no current weapon of it (RW 0x68B58C(0)): nothing. When that weapon's
//     template name IS WeaponThatCausesEvacuation (AsciiString compare RW 0x4065AA): the contain (+ 0x258) evacuates: with no exit interface (contain slot
//     0x74) orderAllPassengersToExit(CMD_FROM_AI) (slot 0x80), else every passenger of the list (slot 0x118) in order: a HORDE (template + 0x115 bit 5)
//     aiHordeExit(me, CMD_FROM_AI) (RW 0x775AFB), anything else aiExit(me, CMD_FROM_AI) (RW 0x7716C1). Any other weapon: a damage of DamageTypeToTrack is
//     tracked (RW 0x8BF09E);
//   * the tracking RW 0x8BF09E: with a body (+ 0x25C) the hit (now, its input amount D + 0x20) is appended (RW 0x777F5C); the sum of the records younger
//     than TrackingTimeSpan (RW 0x8BEF0A: a record whose frame <= now - span is erased on the way) at or above maxHealth (body vslot 0x1C) *
//     DamageToPanicThreshold panics (RW 0x8BF076): with the source object and an AI (+ 0x260) the records are cleared (RW 0x862535) and the AI flees from it
//     (aiFleeFromObject(source, CMD_FROM_AI) RW 0x881FF3: AI command 0x2E).
// INFERENCE / NOT PORTED (fork lane EVACUATE-1, reported once per game): aiFleeFromObject (AI command 0x2E) has no port (AmbientAIUpdate emulates its own
// flee, S-5910): the panic clears the records as retail and flees nowhere. Every ported contain is taken to have the exit interface (OpenContain RW 0x867A94
// answers module + 0x30; no contain answering slot 0x74 with null was read).

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DamageModule.h"

#include <vector>
#include <string>
#include <utility>

class ModuleFactory;
class MultiIniFieldParse;

class EvacuateDamageModuleData : public ModuleData
{
public:
	std::string m_weaponThatCausesEvacuation; ///< + 0x08 ("" none, or a name TheWeaponStore did not know)
	int m_damageTypeToTrack = 3;              ///< + 0x0C (PIERCE)
	float m_damageToPanicThreshold = 0.1f;    ///< + 0x10 (of the body's maximum health)
	unsigned m_trackingTimeSpan = 6;          ///< + 0x14 (frames)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class EvacuateDamage : public BehaviorModule, public DamageModuleInterface
{
public:
	OBFME_MODULE_XFER(EvacuateDamage)
	EvacuateDamage(Thing *thing, const EvacuateDamageModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	DamageModuleInterface *getDamage() override { return this; }
	void onDamage(const DamageInfo &info) override;                                                // RW 0x8BF12E
	void onBodyDamageStateChange(const DamageInfo *, BodyDamageType, BodyDamageType) override {} // RW 0x918BA0
	void crc(StateHasher &hasher) const override;
	static void registerModule(ModuleFactory &modules);
	static const char *stopLine();

	// the tracked hits, oldest first: (frame, input amount) (RW module + 0x14: a list of { + 8 amount, + 0xC frame })
	const std::vector<std::pair<unsigned, float>> &records() const { return m_records; }
	// RW 0x8BEF0A: the records younger than TrackingTimeSpan summed, the older ones erased
	float recentDamage();

private:
	void evacuate();                       // RW 0x8BF178 .. 0x8BF1EA
	void track(const DamageInfo &info);    // RW 0x8BF09E
	void panic(Object &source);            // RW 0x8BF076
	const EvacuateDamageModuleData *m_data;
	std::vector<std::pair<unsigned, float>> m_records;
};

// OpenBFME. GPL-3.0.
//
// StrafeAreaUpdate (fork STRAFE-1, package B of docs/fork/MODULE_CENSUS.md): the flight of the spell book's Dragon Strike dragon (RotWK 2.01 data: one
// template, DragonStrikeDragon: WeaponName DragonStrikeStrafeWeapon, StrafeAreaRadius 180, Sweepfrequency 0.45, SweepAmplitude 70, Slope 50,
// InitialSweepPhase 1.57). A BFME class; ZH has none. Donor: the BFME2 decomp's StrafeAreaUpdate.cpp / StrafeAreaUpdate_update.cpp (update
// 0x003A4FDA, byte-matched; the start helper 0x003A4F21); read again from RotWK's game.dat (md5 daed3668, caveat S-001), the same statements:
//   * registration RW 0x659641 ("StrafeAreaUpdate", RTTI name RW 0xBF7394); the data RW 0x64D410 (0x20 bytes, ctor RW 0x7A81E8, field table RW 0xC31968:
//     WeaponName + 8 (AsciiString), StrafeAreaRadius + 0xC (150), Sweepfrequency + 0x10 (0.4), SweepAmplitude + 0x14 (100), Slope + 0x18 (50),
//     InitialSweepPhase + 0x1C (0), every real RW 0x42ED00); the module RW 0x64D3D8 (0x48 bytes, ctor RW 0x7A80B1: vtables RW 0xC318D4 / 0xC67300 /
//     0xC318C8; target + 0x20, start + 0x2C, phase + 0x38, state + 0x3C, started + 0x40, next fire frame + 0x44, all 0; awake from the next frame).
//   * the start RW 0x7A82AF (from, to), called by the OCL's CreateObject nugget for MoveUsesStrafeUpdate (RW 0x5F282F .. 0x5F2883, the made object's
//     findModule RW 0x68BDA5 of the name key): false without an AI (+ 0x260) or an aircraft one (AI vslot 0x188: only GiantBirdAIUpdate answers, RW
//     0x8CEF91 returns the AI itself); else target = to, start = from, phase = InitialSweepPhase, state 0, started false, the orientation atan2f(to.y -
//     from.y, to.x - from.x) (RW 0x441BF4, SSE differences; RW 0x70C31E), aiMoveToPosition(target, CMD_FROM_AI) (RW 0x66C4CA); true (the OCL then
//     issues no move of its own).
//   * update RW 0x7A8368: no AI or no aircraft: sleep forever. d = |target - position| in x / y (z 0, RW 0x403111). State 0: locomotor set 6
//     (SUPERSONIC, AI vslot 0x238); ATTACKING (model condition 37) when d <= Slope * 2 + Radius; when d <= Slope + Radius: set 0, state 1, the next fire
//     frame = frame + 12. State 1: the weapon (TheWeaponStore by name, RW 0x6CC5DF) fires from the next fire frame on, every frame (the frame is never
//     moved on): phase += Sweepfrequency (SSE), offset = (float)(sin(phase) * SweepAmplitude) (CRT sin RW 0xA3CF90, x87 fmul), the facing f normalised
//     (RW 0x70B9E0, RW 0x403175), at position + f * Slope + (f.y, -f.x, 0) * offset (SSE in the retail operand order) on the ground (TerrainLogic slot
//     0x18), RW 0x6CF530 createAndFireTempWeapon; ATTACKING; then when started and d > Radius - Slope: state 2, set 6, ATTACKING cleared; else when
//     Radius * (1 / 12) > d and the aircraft is idle (vslot 0x1B8, RW 0x769E8F): onward = target + normalise(target - start) * 500, its closest map edge
//     point (TerrainLogic slot 0x34), aiMoveToPosition(CMD_FROM_AI), started. State 2: once d > Radius, idle and not effectively dead (+ 0x458 bit 0):
//     kill(UNRESISTABLE, DEATH_FADED (22)). Sleeps none (1).
//   * xfer RW 0x7A814E: version 3 (RotWK; BFME2 writes 4), UpdateModule's, then (not a light CRC) target, start, phase, state, started. The next fire frame
//     (+ 0x44, version 4 only) is not saved: a loaded strafe fires from frame 0 on, as in retail.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>

class ModuleFactory;

class StrafeAreaUpdateModuleData : public ModuleData
{
public:
	std::string m_weaponName;          ///< + 8
	float m_radius = 150.0f;           ///< + 0xC StrafeAreaRadius
	float m_frequency = 0.4f;          ///< + 0x10 Sweepfrequency
	float m_amplitude = 100.0f;        ///< + 0x14 SweepAmplitude
	float m_slope = 50.0f;             ///< + 0x18 Slope
	float m_initialPhase = 0.0f;       ///< + 0x1C InitialSweepPhase
	static void buildFieldParse(MultiIniFieldParse &p);
};

class StrafeAreaUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(StrafeAreaUpdate)
	StrafeAreaUpdate(Thing *thing, const StrafeAreaUpdateModuleData *data); // RW 0x7A80B1
	UpdateSleepTime update() override;                                      // RW 0x7A8368
	void crc(StateHasher &hasher) const override;
	bool startStrafe(const Coord3D &from, const Coord3D &to);               // RW 0x7A82AF

	// tests
	int state() const { return m_state; }
	bool started() const { return m_started; }
	float phase() const { return m_phase; }
	const Coord3D &target() const { return m_target; }

	static void registerModule(ModuleFactory &modules);

private:
	const StrafeAreaUpdateModuleData *m_data;
	Coord3D m_target{ 0.0f, 0.0f, 0.0f }; ///< + 0x20
	Coord3D m_start{ 0.0f, 0.0f, 0.0f };  ///< + 0x2C
	float m_phase = 0.0f;                 ///< + 0x38
	int m_state = 0;                      ///< + 0x3C
	bool m_started = false;               ///< + 0x40
	unsigned m_nextFire = 0;              ///< + 0x44
};

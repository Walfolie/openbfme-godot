// OpenBFME. GPL-3.0.
//
// Castle modules of fork lane CASTLEMOD-1 (package A of docs/fork/MODULE_CENSUS.md). Read from RotWK's game.dat (md5 daed3668, caveat S-001); the BFME2
// decomp's sources were the reading aid.
//
// OCLUpdate (the castle walls' trebuchet and catapult hard points: GondorCastleWallTrebuchet, ArnorCastleWallTrebuchet, DwarvenCastleWallCatapult):
//   * registration RW 0x659A65 (mask 1: update), create RW 0x64E283 (0x2C bytes, constructor RW 0x89EB48 over UpdateModule's RW 0x653114), data RW 0x64E2BB
//     (0x1C bytes, RW 0x89EB1C: Amount -1, the rest 0); table RW 0xC67420: OCL + 0x08 (RW 0x73A368), MinDelay + 0x0C / MaxDelay + 0x10 (ms to frames,
//     RW 0x73A429), CreateAtEdge + 0x18 (parseBool), Amount + 0x14 (parseInt);
//   * the module: + 0x20 the next creation frame, + 0x24 the frame it was set, + 0x28 the number made; xfer RW 0x89EBE8: UpdateModule's, then (not for a
//     light CRC) version 2, + 0x20, + 0x24, from version 2 + 0x28;
//   * update RW 0x89EC43: when the frame is at least the next creation frame and the object is not UNDER_CONSTRUCTION (RW 0x89EB99): a module that never
//     set the frame (0) only sets it; else the frame is set again (RW 0x89EBBA: GetGameLogicRandomValue(MinDelay, MaxDelay, line 0x7B) from now) and the
//     OCL is made (RW 0x5F00CA: the object, at the object's position or, with CreateAtEdge, TheTerrainLogic's closest edge point, RW 0x67D949, the object's
//     position as the second); with Amount above 0 the module sleeps forever once it made Amount. It runs every frame otherwise;
//   * the control bar's countdown reads RW 0x89EA88 (1 - remaining / whole) and RW 0x89EACA (remaining frames).
// INFERENCE: the OCL is made by the port's ObjectCreationList::create at the creation point (S-530).
//
// RubbleRiseUpdate (the camp keeps, IsengardCastleBaseKeep, AngFornostSmallSpire: their rubble rises out of the ground after the collapse):
//   * registration RW 0x659DF0 (mask 3: update and die), create RW 0x64EAB7 (0x48 bytes, constructor RW 0x8A8325: asleep), data RW 0x6563F3 (0xD8
//     bytes, RW 0x656350: MinBurstDelay 9999, the counts 1, the rest 0); table RW 0xC69408 then the DieMux table (RW 0x8D296A): MinRubbleRiseDelay +
//     0x38 / MaxRubbleRiseDelay + 0x3C (not read by the update), MinBurstDelay + 0x40, MaxBurstDelay + 0x44 (ms to frames), RubbleRiseDamping + 0x4C,
//     RubbleHeight + 0x50 (not read), MaxShudder + 0x54, BigBurstFrequency + 0x48, OCL / FXList (RW 0x8A8A82 / 0x8A8A1D: a phase of RW 0xDB0ED0 INITIAL,
//     DELAY, BURST, FINAL, then the names);
//   * the module: + 0x24, + 0x28 the next burst, + 0x2C the state (waiting, rising, done), + 0x30 the velocity, + 0x34 the height risen, + 0x38 the
//     rubble's height, + 0x3C the start position;
//   * onDie RW 0x8A83F6: an applicable death (RW 0x8D29A9) resets the state (RW 0x8A83CE) and wakes the module;
//   * update RW 0x8A85D0: waiting, with a drawable and model condition POST_RUBBLE (59): the INITIAL phase at the object (RW 0x8A847C: a distinct random
//     pick per entry of the count from each list, line 272; every FX at the position, the OCLs made by the object), the drawable's shudder (client random,
//     the instance matrix: client), the BURST phase, the next burst now + random(MinBurstDelay, MaxBurstDelay) (line 0xAB), the velocity -Gravity * (1 -
//     RubbleRiseDamping) * 20 (x87), the rubble's height the terrain's layer height under the object (TheTerrainLogic vslot 0x1C, RW 0x68BBE0), the start
//     position; rising: the height grows by the velocity, the object stands at the start plus two random reals of +- MaxShudder (line 0xC4; the first to y)
//     and the height; at the next burst the BURST phase when random(1, BigBurstFrequency) is 1 (line 0xCD), else DELAY, and the next burst moves on by
//     random(MinBurstDelay, MaxBurstDelay) (line 0xD6); once the height reaches the rubble's height less the start's z: the object at the rubble's height,
//     done, the FINAL phase, POST_RUBBLE cleared and POST_COLLAPSE (60) set, the orientation set again; the module sleeps.
// NOT PORTED: the bone FX stop at the end (RW 0x8A841C, as StructureCollapseUpdate's), the body's particle refresh (body vslot 0x2C, client) and the
// drawable's instance matrix (client); a drawable is taken to exist (every object of a game has one).

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class ModuleFactory;
class MultiIniFieldParse;
class StateHasher;

class OCLUpdateModuleData : public ModuleData
{
public:
	std::string m_ocl;          ///< + 0x08
	UnsignedInt m_minDelay = 0; ///< + 0x0C (frames)
	UnsignedInt m_maxDelay = 0; ///< + 0x10
	int m_amount = -1;          ///< + 0x14
	bool m_createAtEdge = false; ///< + 0x18
	static void buildFieldParse(MultiIniFieldParse &p);
};

class OCLUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(OCLUpdate)
	OCLUpdate(Thing *thing, const OCLUpdateModuleData *data); // RW 0x89EB48
	static void registerModule(ModuleFactory &modules);
	UpdateSleepTime update() override; // RW 0x89EC43
	void crc(StateHasher &hasher) const override;

	UnsignedInt nextCreationFrame() const { return m_nextFrame; }
	int made() const { return m_made; }
	float countdownPercent() const;    // RW 0x89EA88
	UnsignedInt remainingFrames() const; // RW 0x89EACA

private:
	void setNextCreationFrame(); // RW 0x89EBBA
	const OCLUpdateModuleData *m_data;
	UnsignedInt m_nextFrame = 0;  ///< + 0x20
	UnsignedInt m_setFrame = 0;   ///< + 0x24
	int m_made = 0;               ///< + 0x28
};

enum RubbleRisePhase
{
	RRPHASE_INITIAL = 0,
	RRPHASE_DELAY,
	RRPHASE_BURST,
	RRPHASE_FINAL,
	RRPHASE_COUNT
};

class RubbleRiseUpdateModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;                 ///< + 0x08
	unsigned m_minRubbleRiseDelay = 0;   ///< + 0x38 (frames)
	unsigned m_maxRubbleRiseDelay = 0;   ///< + 0x3C
	unsigned m_minBurstDelay = 9999;     ///< + 0x40
	unsigned m_maxBurstDelay = 0;        ///< + 0x44 (not set by the retail constructor)
	int m_bigBurstFrequency = 0;         ///< + 0x48
	float m_rubbleRiseDamping = 0.0f;    ///< + 0x4C
	float m_rubbleHeight = 0.0f;         ///< + 0x50
	float m_maxShudder = 0.0f;           ///< + 0x54
	std::vector<std::string> m_ocl[RRPHASE_COUNT]; ///< + 0x58
	std::vector<std::string> m_fx[RRPHASE_COUNT];  ///< + 0x88
	int m_oclCount[RRPHASE_COUNT] = { 1, 1, 1, 1 }; ///< + 0xB8
	int m_fxCount[RRPHASE_COUNT] = { 1, 1, 1, 1 };  ///< + 0xC8
	static void buildFieldParse(MultiIniFieldParse &p);
};

class RubbleRiseUpdate : public UpdateModule, public DieModuleInterface
{
public:
	OBFME_MODULE_XFER(RubbleRiseUpdate)
	enum State
	{
		WAITING = 0,
		RISING,
		DONE
	};
	RubbleRiseUpdate(Thing *thing, const RubbleRiseUpdateModuleData *data); // RW 0x8A8325
	static void registerModule(ModuleFactory &modules);
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override; // RW 0x8A83F6
	UpdateSleepTime update() override;                           // RW 0x8A85D0
	void crc(StateHasher &hasher) const override;
	State state() const { return (State)m_state; }
	float height() const { return m_height; }
	float rubbleHeight() const { return m_rubbleHeight; }

private:
	void reset();                                             // RW 0x8A83CE
	void doPhase(RubbleRisePhase phase, const Coord3D &where); // RW 0x8A847C
	const RubbleRiseUpdateModuleData *m_data;
	UnsignedInt m_unused24 = 0;   ///< + 0x24
	UnsignedInt m_nextBurst = 0;  ///< + 0x28
	int m_state = WAITING;        ///< + 0x2C
	float m_velocity = 0.0f;      ///< + 0x30
	float m_height = 0.0f;        ///< + 0x34
	float m_rubbleHeight = 0.0f;  ///< + 0x38
	Coord3D m_start{ 0.0f, 0.0f, 0.0f }; ///< + 0x3C
};

namespace CastleModules2
{
void registerAll(ModuleFactory &modules);
}

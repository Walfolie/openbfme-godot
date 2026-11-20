// OpenBFME. GPL-3.0.
//
// DynamicShroudClearingRangeUpdate (fork SHROUDPING-1, package B of docs/fork/MODULE_CENSUS.md): the module of the object MapRevealPing (RotWK 2.01
// system.ini), which the OCL OCL_SUPERWEAPON_RevealArea makes; no special power or command button of RotWK's data names that OCL (a map script can).
// Donor: the BFME2 decomp's DynamicShroudClearingRangeUpdate.cpp (the Zero Hour statements); read again from RotWK's game.dat (md5 daed3668, caveat
// S-001), the same statements:
//   * registration RW 0x658C92 ("DynamicShroudClearingRangeUpdate", RW 0xC0B7CC); data RW 0x64CAE2 -> ctor RW 0x88EC37 (ShrinkDelay + 8, ShrinkTime
//     + 0xC, GrowDelay + 0x10, GrowTime + 0x14, ChangeInterval + 0x1C, GrowInterval + 0x20 = 0, FinalVision + 0x18 = 0, + 0x24 false, the
//     RadiusDecalTemplate + 0x28 RW 0x732888); table RW 0xC62648: the six durations parseDurationUnsignedInt RW 0x73A429, FinalVision parseReal
//     RW 0x42ED00, GridDecalTemplate RW 0x7327BC (initFromINI with RW 0xC24278, GameClient/RadiusCursor.h).
//   * module RW 0x64CB1D (0x5C bytes) -> ctor RW 0x88E6D8: the countdown + 0x24 = ShrinkDelay + ShrinkTime, the total + 0x28 = max(1, countdown), the
//     deadlines grow start + 0x2C = countdown - GrowDelay, sustain + 0x30 = grow start - GrowTime, shrink start + 0x34 = countdown - ShrinkDelay, the
//     done-forever frame + 0x38 = the logic frame + countdown, the change countdown + 0x3C = 0, + 0x40 (decals made) false, + 0x44 = 0, the native range
//     + 0x48 = 200, the current range + 0x4C = 0, the state + 0x20 = 0; with a controlling player (RW 0x68B678) the native range is getShroudClearingRange
//     (RW 0x68E4E2, ShroudManager::visionRange) and the 30 grid decals (+ 0x50, 16 bytes each) are cleared.
//   * update RW 0x88E9B0 (every frame, returns 1: UPDATE_SLEEP_NONE): state 5 returns at once. The decals are made at the first update (RW 0x88E87F,
//     radius 100). The state: countdown <= 0 or frame > done-forever (unsigned) DONE (4); else countdown (unsigned) <= shrink start SHRINKING (3),
//     <= sustain SUSTAINING (2), <= grow start GROWING (1), else unchanged. NOT_STARTED animates the decals (RW 0x88E8C7); GROWING animates and adds
//     native / max(1, GrowTime), SUSTAINING (2) when it reached native (the next frame's classification wins again); SUSTAINING sets native and kills
//     the decals (RW 0x88E6AF); SHRINKING subtracts (native - FinalVision) / max(1, ShrinkTime); DONE kills the decals and sets FinalVision (SSE single
//     throughout, the durations as unsigned floats). Then the countdown down by 1 while > 0; the change countdown down by 1 while > 0, else it takes
//     GrowInterval (GROWING) or ChangeInterval, Object::setShroudClearingRange(current) (RW 0x68C234) and DONE becomes SLEEPING (5).
//   * xfer RW 0x88EB76: UpdateModule's (RW 0x850C60), the light CRC returns, version 1, + 0x24 .. + 0x4C (not the state: a load starts NOT_STARTED).
// The grid decals (RadiusDecal, TheProjectedShadowManager) are the client's and not drawn: stop S-7177.

#pragma once

#include "Common/INI.h"
#include "GameClient/RadiusCursor.h"
#include "GameLogic/Module/UpdateModule.h"

class ModuleFactory;

class DynamicShroudClearingRangeUpdateModuleData : public ModuleData
{
public:
	unsigned m_shrinkDelay = 0;    ///< + 8 ShrinkDelay (frames)
	unsigned m_shrinkTime = 0;     ///< + 0xC ShrinkTime
	unsigned m_growDelay = 0;      ///< + 0x10 GrowDelay
	unsigned m_growTime = 0;       ///< + 0x14 GrowTime
	float m_finalVision = 0.0f;    ///< + 0x18 FinalVision
	unsigned m_changeInterval = 0; ///< + 0x1C ChangeInterval
	unsigned m_growInterval = 0;   ///< + 0x20 GrowInterval
	RadiusDecalTemplate m_gridDecalTemplate; ///< + 0x28 GridDecalTemplate
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DynamicShroudClearingRangeUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(DynamicShroudClearingRangeUpdate)

	enum State
	{
		NOT_STARTED_YET = 0,
		GROWING = 1,
		SUSTAINING = 2,
		SHRINKING = 3,
		DONE_FOREVER = 4,
		SLEEPING = 5
	};

	DynamicShroudClearingRangeUpdate(Thing *thing, const DynamicShroudClearingRangeUpdateModuleData *data); // RW 0x88E6D8
	UpdateSleepTime update() override;                                                                     // RW 0x88E9B0
	void crc(StateHasher &hasher) const override;

	// tests
	const DynamicShroudClearingRangeUpdateModuleData &data() const { return *m_data; }
	int state() const { return m_state; }
	int stateCountDown() const { return m_stateCountDown; }
	unsigned growStartDeadline() const { return m_growStartDeadline; }
	unsigned sustainDeadline() const { return m_sustainDeadline; }
	unsigned shrinkStartDeadline() const { return m_shrinkStartDeadline; }
	float nativeClearingRange() const { return m_nativeClearingRange; }
	float currentClearingRange() const { return m_currentClearingRange; }

	static void registerModule(ModuleFactory &modules);

private:
	const DynamicShroudClearingRangeUpdateModuleData *m_data;
	int m_state = NOT_STARTED_YET;          ///< + 0x20
	int m_stateCountDown = 0;               ///< + 0x24
	int m_totalFrames = 1;                  ///< + 0x28
	unsigned m_growStartDeadline = 0;       ///< + 0x2C
	unsigned m_sustainDeadline = 0;         ///< + 0x30
	unsigned m_shrinkStartDeadline = 0;     ///< + 0x34
	unsigned m_doneForeverFrame = 0;        ///< + 0x38
	unsigned m_changeIntervalCountdown = 0; ///< + 0x3C
	bool m_decalsCreated = false;           ///< + 0x40
	float m_visionChangePerInterval = 0.0f; ///< + 0x44 (set 0, never read)
	float m_nativeClearingRange = 200.0f;   ///< + 0x48
	float m_currentClearingRange = 0.0f;    ///< + 0x4C
};

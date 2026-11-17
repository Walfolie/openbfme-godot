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
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
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

// WallUpgradeUpdate (the castles' upgrade points: GondorCastleUpgrade, RohanCastleUpgrade, MinisWallAUpgrade, ...):
//   * registration RW 0x659F98 (mask 5: update and damage), create RW 0x64EFD7 (0x34 bytes, constructor RW 0x8AEA12: + 0x28 0, + 0x2C 0, + 0x30 true,
//     + 0x31 false, awake next frame), data RW 0x65428C (the plain ModuleData, no fields);
//   * update RW 0x8AEBD3: the first run scans once (RW 0x8AEB10: with no building yet the closest object within 200 (RW 0xBE4170, FROM_CENTER_3D) of
//     KindOf STRUCTURE and WALK_ON_TOP_OF_WALL, not this object, becomes it); a countdown runs (awake every frame) and at 0 raises the revive; with the
//     revive, when the building stands (RW 0x8AEBA6: found, not effectively dead, no ACTIVELY_BEING_CONSTRUCTED) the object's upgrades are reset (RW
//     0x68E083), its body set PRISTINE (body slot 0x28, RW 0x8C1BF9), DYING cleared and SOLD, UNSELECTABLE, BUILD_BEING_CANCELED, NO_COLLISIONS cleared (RW
//     0x68D440); else it looks again in LOGICFRAMES_PER_SECOND frames;
//   * onBodyDamageStateChange RW 0x8AEAE6: entering RUBBLE from a lower state starts the countdown at 8 seconds and wakes the module;
//   * the interface at + 0x24 (RW 0x8AEAA9): true when the object has UPGRADE_TREBUCHET, UPGRADE_POSTERN_GATE or UPGRADE_GARRISON;
//   * xfer RW 0x8AE96E: version 1, UpdateModule's, + 0x28, + 0x2C, + 0x30, + 0x31.
// NOT PORTED: RW 0x8AEC79 (GeometryUpgrade calls it after a geometry change: the AI units around the object on the ground layer whose cell does not take
// them, RW 0x6EF3C3, are moved out by three times the geometry centre's offset and faded in): the pathfinder's occupancy test RW 0x6EF3C3 is not ported
// (S-166); stop S-1423.
class WallUpgradeUpdate : public UpdateModule, public DamageModuleInterface
{
public:
	OBFME_MODULE_XFER(WallUpgradeUpdate)
	WallUpgradeUpdate(Thing *thing, const ModuleData *data); // RW 0x8AEA12
	static void registerModule(ModuleFactory &modules);
	DamageModuleInterface *getDamage() override { return this; }
	UpdateSleepTime update() override; // RW 0x8AEBD3
	void onDamage(const DamageInfo &) override {}
	void onBodyDamageStateChange(const DamageInfo *info, BodyDamageType oldState, BodyDamageType newState) override; // RW 0x8AEAE6
	void crc(StateHasher &hasher) const override;
	bool hasUpgradeCondition() const; // RW 0x8AEAA9
	ObjectID building() const { return m_building; }
	int countdown() const { return m_countdown; }

private:
	void scanForBuilding();  // RW 0x8AEB10
	bool buildingStands() const; // RW 0x8AEBA6
	ObjectID m_building = INVALID_ID; ///< + 0x28
	int m_countdown = 0;              ///< + 0x2C
	bool m_scanPending = true;        ///< + 0x30
	bool m_revive = false;            ///< + 0x31
};

// ManTheWallsSpecialPower (GondorGreatKeep, ArnorGreatKeep: SPECIAL_MAN_THE_WALLS):
//   * registration RW 0x65B809, create RW 0x6521DF (0x34 bytes, constructor RW 0x8CB3D1 over SpecialPowerModule's RW 0x8973EE), the plain
//     SpecialPowerModuleData (RW 0x65247C); the special power interface is SpecialPowerModule's (RW 0xC64FF0) except doSpecialPower (slot 0x28, RW 0x8CBDBA);
//   * doSpecialPower RW 0x8CBDBA (SpecialPowerModule's own is not called): the spots (RW 0x8CBCCE: every object, TheGameLogic's list through RW 0xA39420, of
//     KindOf TACTICAL_MARKER that is this object's ally, RW 0x660B85 mask 4) each take a record with no members; none: nothing. Then every HORDE that is an
//     ally, alive (RW 0xC10E20) and on this object's side of the map (RW 0xC0F374) goes to one record (RW 0x8CBB53 -> 0x8CBACB): the records sorted by the
//     squared 2D distance of the spot (SSE dy * dy + dx * dx, RW 0x8CBA40; STLport sort RW 0x8CB9E4 with RW 0x8CB401), the nearest whose member count is at
//     most 0, else at most 1, ... at most 9, else the nearest; last every record with members moves them as a group to its spot (RW 0x8CB5A6: an AIGroup
//     of the members in order, groupMoveToPosition RW 0x774897 with no waypoint, target, facing or formation, source 0).
class ManTheWallsSpecialPower : public SpecialPowerModule
{
public:
	OBFME_MODULE_XFER(ManTheWallsSpecialPower)
	ManTheWallsSpecialPower(Thing *thing, const SpecialPowerModuleData *data) : SpecialPowerModule(thing, data) {} // RW 0x8CB3D1
	static void registerModule(ModuleFactory &modules);
	void doSpecialPower(unsigned options) override; // RW 0x8CBDBA
	// tests: the spot each horde was sent to by the last use, in send order (spot id, horde id)
	const std::vector<std::pair<ObjectID, ObjectID>> &lastOrders() const { return m_lastOrders; }

private:
	std::vector<std::pair<ObjectID, ObjectID>> m_lastOrders; ///< diagnostics (not retail state: not hashed, not saved)
};

namespace CastleModules2
{
void registerAll(ModuleFactory &modules);
}

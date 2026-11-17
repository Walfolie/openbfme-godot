// OpenBFME. GPL-3.0.
//
// RunOffMapBehavior (fork RUNOFF-1, package B of docs/fork/MODULE_CENSUS.md): the riderless horses of Gondor's and Rohan's cavalry
// (GondorCavalryRiderless, TheodenCavalryRiderless, made by the cavalry's death OCLs) run away and leave; the Mumakil's (RequiresSpecificTrigger,
// only DetachableRiderUpdate triggers it, a cinematic module) never runs in a 2.01 multiplayer game. A BFME class; ZH has none. Ported from the
// RotWK binary (caveat S-001); the BFME2 decomp (RunOffMapBehavior.cpp, RunOffMapBehaviorCtor.cpp) has the same body.
//
// TARGET FACTS (each read from the disassembly):
//   * registration RW 0x658715; data RW 0x654C73 (0x1C bytes, constructor RW 0x654C1D): RunToLocation + 0x08 (Coord2D, default 10, 10; nothing reads
//     it), RequiresSpecificTrigger + 0x10, RunOffMapWaypointName + 0x14, DieOnMap + 0x18 (table RW 0xC59698); module RW 0x64B39A (0x14 bytes, constructor
//     RW 0x865289: BehaviorModule's RW 0x6530B4, vtables RW 0xC597A4 / 0xC596E8 / 0xC59644, the last the run-off interface at + 0x10, which behaviour
//     interface slot 0x8C answers, RW 0x863BF3).
//   * onObjectCreated (module slot 5, RW 0x865321): without RequiresSpecificTrigger the flee starts at once. It runs in the Object constructor (RW
//     0x69A637), so before an OCL places the object: the object stands at its creation position then.
//   * triggerFlee (interface slot 0, RW 0x86540A): nothing without an AI (+ 0x260) or module data. The destination is the waypoint of that name (terrain
//     slot 0x88, + 0xC), else the position plus 500 (RW 0xBDD97C) times the unit direction (RW 0x70B9E0; SSE mulss / addss) at the ground height of that
//     point (terrain slot 0x18). Without a path there (Pathfinder::quickDoesPathExist RW 0x6F5BB0, no locomotor set) the last damage record's source (body
//     slot 0x40 + 8) is credited with the kill (RW 0x6955BC type 1) and the object killed (RW 0x698EC3: UNRESISTABLE, NORMAL). Else the AI is given
//     aiMoveToAndDie (DieOnMap) or aiMoveToAndDelete with CMD_FROM_AI (RW 0x86539E / 0x865332: AI commands 0x41 / 0x38), the object takes the status
//     FLEE_OFF_MAP (RW 0x62684D(0x32, 1)) and the model condition RUNNING_OFF_MAP (+ 0x128 bit 28 = 252, RW 0x68B53C).
//   * xfer RW 0x9933FE: version 1 (RW 0x404F15), then BehaviorModule's (RW 0x8CD4AA): no field of its own.
// Object RW 0x699282 (run off or die: the interface's triggerFlee after the object leaves its container, else the kill) has two RotWK callers, the AI
// states 45 and 74 (RW 0x75187A / 0x751CC5), and DetachableRiderUpdate's die (RW 0x8B2D59); neither is ported (stop S-4983).

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/Module.h"
#include "GameLogic/Module/BehaviorModule.h"

#include <string>

class ModuleFactory;
class MultiIniFieldParse;

class RunOffMapBehaviorModuleData : public ModuleData
{
public:
	Coord2D m_runToLocation{ 10.0f, 10.0f }; ///< + 0x08 (RW 0xBD83D8)
	bool m_requiresSpecificTrigger = false;  ///< + 0x10
	std::string m_waypointName;              ///< + 0x14
	bool m_dieOnMap = false;                 ///< + 0x18
	static void buildFieldParse(MultiIniFieldParse &p);
};

class RunOffMapBehavior : public BehaviorModule
{
public:
	OBFME_MODULE_XFER(RunOffMapBehavior)
	RunOffMapBehavior(Thing *thing, const RunOffMapBehaviorModuleData *data) : BehaviorModule(thing, data), m_data(data) {} // RW 0x865289
	static void registerModule(ModuleFactory &modules);
	void onObjectCreated() override; // RW 0x865321
	void triggerFlee();              // RW 0x86540A

private:
	const RunOffMapBehaviorModuleData *m_data;
};

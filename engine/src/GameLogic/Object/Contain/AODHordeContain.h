// OpenBFME. GPL-3.0.
//
// AODHordeContain (fork lane AOD-1, part 2): the Army of the Dead's horde (RohanOathbreakerHorde / RohanOathbreakerHordeSmall, made by Aragorn's
// GondorArmyofTheDeadEgg). A HordeContain whose ghosts float along the horde's own trail, sway, flow up over a LARGE unit they meet and scatter when
// their oath is fulfilled. Read from RotWK's game.dat (md5 daed3668, caveat S-001); the BFME2 decomp's AODHordeContain sources were the reading aid
// (the class layout: HordeContain up to + 0x30C, module size 0x8E0). Notes: workspace/re/aod_notes.md.
//
// TARGET FACTS:
//   * the class (constructor RW 0x87E647 over HordeContain's RW 0x8727F9; vtables RW 0xC5D3F0, contain RW 0xC5D268, horde interface RW 0xC5CFA8)
//     overrides against HordeContain: buildSlots (slot 0x84, RW 0x87E8FC), the member pass (0x8C, RW 0x87D9F6), the member order (0x90, RW 0x87DB01),
//     the fill-in (0x94: RW 0x9188EB, false: freed slots are never filled in), onContaining (contain 0x58, RW 0x87D649), isPassengerAllowedToFire
//     (contain 0xB4, RW 0x9188EB: false), the interface's slot 8 (RW 0x63F3BF: nothing; HordeContain's RW 0x87545C, not ported), slot 7 (the member's
//     position, RW 0x87DEFB) and slot 0x130 (a member's AODCrushCollide contact, RW 0x87E894);
//   * the data (RW 0x87D55F after HordeContainModuleData's RW 0x878EE5; table RW 0xC5CE50 after HordeContain's): FrequencyScale + 0x284,
//     FrequencyRandomness + 0x288, AmplitudeScale + 0x28C, AmplitudeRandomness + 0x290, StillAmplitude + 0x294, the five Z twins + 0x298 .. + 0x2A8 (all
//     0), OathFulfilledZFactor + 0x2AC (1.0), LargeUnitHeightFactor + 0x2B0 (0.45), LargeUnitMinHeight + 0x2B4 (0), LargeUnitMaxHeight + 0x2B8 (999999),
//     LargeUnitTimeout + 0x2BC (ms to frames, RW 0x73A429; 2), LargeUnitTailOff + 0x2C0 (1.0), ScatterSpeedFactor + 0x2C4 (0.3), ScatterRandomness + 0x2C8
//     (0.4);
//   * the module's own state: + 0x30C one sway record per slot (6 floats), + 0x318 the LARGE unit followed, + 0x31C the frame it was met, + 0x320 its
//     position, + 0x32C 17.0, + 0x330 its geometry's top (RW 0xAD1920), + 0x334 the distance walked since the oath was fulfilled, + 0x338 the trail (60
//     points: position and angle), + 0x6F8 its length, + 0x6FC the rank lines (20: the slots' distinct X, an angle, a position), + 0x8DC their count;
//   * buildSlots: HordeContain's (RW 0x877751); per slot six GameLogicRandomValueReal draws (AODHordeContain.cpp lines 0x20A .. 0x211): amplitude
//     1 +- AmplitudeRandomness, phase 0 .. FrequencyRandomness * 2 pi, frequency 1 +- FrequencyRandomness, the same three for Z; then the rank lines from
//     the slots' distinct X (in slot order, at most 20), each at the horde's position plus its X less the first's turned by the horde's angle, facing the
//     horde; the trail starts with the rank lines' positions, the last one first;
//   * the member pass: the horde's position joins the trail when it is farther than its locomotor's acceleration (100 / 10 per second without one) from
//     the first rank line; every rank line takes its place along the trail at its X behind the first (turning at most 10 degrees a frame toward the trail's
//     angle there); with OATH_FULLFILLED the distance grows by the horde's speed; then HordeContain's pass;
//   * a member's position (slot 7): its rank line's position with the slot's Y across it; then (slots built) the sway: sin(frame * FrequencyScale *
//     frequency + phase) * amplitude * AmplitudeScale scaled from StillAmplitude to 1 by the horde's speed fraction, the same for Z (+ 0.95, on the ground
//     or the water); a StillAmplitude above 0.01 keeps the pass running (the contain's dirty byte); OATH_FULLFILLED scatters the member outward by
//     ScatterSpeedFactor and ScatterRandomness (slot % 5) times the distance; a LARGE unit met (slot 0x130) lifts the horde over it (LargeUnitHeightFactor
//     times its top, clamped, falling off over LargeUnitTailOff times that height around it and over the second half of LargeUnitTimeout); oath scatter in Z;
//     while the horde melees HordeContain's slot 7;
//   * the member order: a JUST_BUILT member HordeContain's order (then JUST_BUILT cleared); a member whose AI state is active only loses MOVING; while the
//     horde melees the hub (RW 0x87468B); else the member takes the horde's MOVING and OATH_FULLFILLED, is moved straight toward the position by at most
//     1.25 times its locomotor's speed and turned to its rank line's angle (OATH_FULLFILLED: toward the position by at most its turn rate) (RW 0x70BA76,
//     0x70C201), its pending position set and its shock stun cleared (RW 0x792A69(0));
//   * onContaining: the member's drawable fades in over LOGICFRAMES_PER_SECOND * 5 (RW 0x670AA2), it gains a level (RW 0x79D7AD(1)), HordeContain's;
//   * xfer RW 0x87E567: HordeContain's, + 0x318, + 0x334, + 0x31C (re-reading the LARGE unit), + 0x320.
// NOT retail: the save holds the whole state (the trail, the rank lines and the sway records too: a loaded game goes on as the saved one; retail starts
// with an empty trail); the members' CRT sin / cos / sqrt are the deterministic SimMath ones (S-167).

#pragma once

#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

#include <array>
#include <vector>

class AODHordeContainBehaviorData : public HordeContainBehaviorData
{
public:
	float m_frequencyScale = 0.0f;        ///< + 0x284
	float m_frequencyRandomness = 0.0f;   ///< + 0x288
	float m_amplitudeScale = 0.0f;        ///< + 0x28C
	float m_amplitudeRandomness = 0.0f;   ///< + 0x290
	float m_stillAmplitude = 0.0f;        ///< + 0x294
	float m_frequencyScaleZ = 0.0f;       ///< + 0x298
	float m_frequencyRandomnessZ = 0.0f;  ///< + 0x29C
	float m_amplitudeScaleZ = 0.0f;       ///< + 0x2A0
	float m_amplitudeRandomnessZ = 0.0f;  ///< + 0x2A4
	float m_stillAmplitudeZ = 0.0f;       ///< + 0x2A8
	float m_oathFulfilledZFactor = 1.0f;  ///< + 0x2AC
	float m_largeUnitHeightFactor = 0.45f; ///< + 0x2B0
	float m_largeUnitMinHeight = 0.0f;    ///< + 0x2B4
	float m_largeUnitMaxHeight = 999999.0f; ///< + 0x2B8
	UnsignedInt m_largeUnitTimeout = 2;   ///< + 0x2BC (frames)
	float m_largeUnitTailOff = 1.0f;      ///< + 0x2C0
	float m_scatterSpeedFactor = 0.3f;    ///< + 0x2C4
	float m_scatterRandomness = 0.4f;     ///< + 0x2C8
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AODHordeContain : public HordeContain
{
public:
	OBFME_MODULE_XFER(AODHordeContain)
	AODHordeContain(Thing *thing, const AODHordeContainBehaviorData *data); // RW 0x87E647
	static void registerModule(ModuleFactory &modules);

	void onObjectCreated() override;                                      // module slot 0x84 (RW 0x87E8FC)
	bool isPassengerAllowedToFire() const override { return false; }      // contain slot 0xB4 (RW 0x9188EB)
	Coord3D getMemberFormationPosition(const Object *member) const override; // interface slot 7 (RW 0x87DEFB)
	void noteMemberContact(Object *member, Object *other) override;     // interface slot 0x130 (RW 0x87E894)
	void crc(StateHasher &hasher) const override;

	struct TrailPoint
	{
		Coord3D pos{ 0.0f, 0.0f, 0.0f };
		float angle = 0.0f;
	};
	struct RankLine
	{
		float dist = 0.0f; ///< the slots' X
		float angle = 0.0f;
		Coord3D pos{ 0.0f, 0.0f, 0.0f };
		bool flag = false;
	};
	static constexpr int kTrailSize = 60;
	static constexpr int kRankLines = 20;

	// tests, diagnostics
	int trailCount() const { return m_trailCount; }
	const TrailPoint &trailPoint(int i) const { return m_trail[(size_t)i]; }
	int rankLineCount() const { return m_rankCount; }
	const RankLine &rankLine(int i) const { return m_ranks[(size_t)i]; }
	const std::vector<std::array<float, 6>> &sway() const { return m_sway; }
	ObjectID largeUnit() const { return m_largeUnit; }
	float oathDistance() const { return m_oathDistance; }

private:
	bool runMemberPass() override;                                                              // RW 0x87D9F6
	void memberOrder(Object &member, const Coord3D &goal, float orientation, bool force) override; // RW 0x87DB01
	Coord3D memberPassSlot(Object &member, const HordeContainCore::Placement &owner, const HordeAIUpdate *meleeHai, float &slotAngle) override;
	void onMemberContaining(Object *obj) override;                                              // RW 0x87D649

	void trailPush(const Coord3D &pos);  // RW 0x87D9A1
	void updateRankLines();              // RW 0x87D691
	void refreshLargeUnit();             // RW 0x87E516
	int rankLineOf(float x) const;       // the search of RW 0x87DBBB / 0x87DF43
	Coord3D intendedPosition(const Object &member); // RW 0x87DEFB

	const AODHordeContainBehaviorData *m_aod;
	std::vector<std::array<float, 6>> m_sway;            ///< + 0x30C: phase, amplitude, frequency, phaseZ, amplitudeZ, frequencyZ (drawn amplitude, phase, frequency)
	ObjectID m_largeUnit = INVALID_ID;                   ///< + 0x318
	UnsignedInt m_largeUnitFrame = 0;                    ///< + 0x31C
	Coord3D m_largeUnitPos{ 0.0f, 0.0f, 0.0f };          ///< + 0x320
	float m_largeUnitRadius = 0.0f;                      ///< + 0x32C
	float m_largeUnitHeight = 0.0f;                      ///< + 0x330
	float m_oathDistance = 0.0f;                         ///< + 0x334
	std::array<TrailPoint, kTrailSize> m_trail{};        ///< + 0x338
	int m_trailCount = 0;                                ///< + 0x6F8
	std::array<RankLine, kRankLines> m_ranks{};          ///< + 0x6FC
	int m_rankCount = 0;                                 ///< + 0x8DC
};

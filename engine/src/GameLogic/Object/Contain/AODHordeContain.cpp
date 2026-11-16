// OpenBFME. GPL-3.0.
// See GameLogic/Object/Contain/AODHordeContain.h (fork lane AOD-1: RotWK 2.01 RW 0x87D55F .. 0x87EBEE).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Object/Contain/AODHordeContain.h"

#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/OathbreakerModules.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
using Data = AODHordeContainBehaviorData;
const char *const kFile = "E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\Object\\Contain\\HordeContain\\AODHordeContain.cpp"; // RW 0xC5D4A0

const float kTurnStep = 0.174532935f;   // RW 0xC5B69C (10 degrees)
const float kNoSegment = 99999.8984f;   // RW 0xC112D8
const float kTwoPi = 6.28318548f;       // RW 0xBDD38C
const float kLargeUnitRadius = 17.0f;   // RW 0xBDC4DC

// RW 0xC5CE50 (FrequencyRandomness and AmplitudeRandomness, and their Z twins, are listed twice in the table: one row each here)
const FieldParse kFields[] = {
	{ "FrequencyScale", INI::parseReal, nullptr, (int)offsetof(Data, m_frequencyScale) },
	{ "FrequencyRandomness", INI::parseReal, nullptr, (int)offsetof(Data, m_frequencyRandomness) },
	{ "AmplitudeScale", INI::parseReal, nullptr, (int)offsetof(Data, m_amplitudeScale) },
	{ "AmplitudeRandomness", INI::parseReal, nullptr, (int)offsetof(Data, m_amplitudeRandomness) },
	{ "StillAmplitude", INI::parseReal, nullptr, (int)offsetof(Data, m_stillAmplitude) },
	{ "FrequencyScaleZ", INI::parseReal, nullptr, (int)offsetof(Data, m_frequencyScaleZ) },
	{ "FrequencyRandomnessZ", INI::parseReal, nullptr, (int)offsetof(Data, m_frequencyRandomnessZ) },
	{ "AmplitudeScaleZ", INI::parseReal, nullptr, (int)offsetof(Data, m_amplitudeScaleZ) },
	{ "AmplitudeRandomnessZ", INI::parseReal, nullptr, (int)offsetof(Data, m_amplitudeRandomnessZ) },
	{ "StillAmplitudeZ", INI::parseReal, nullptr, (int)offsetof(Data, m_stillAmplitudeZ) },
	{ "OathFulfilledZFactor", INI::parseReal, nullptr, (int)offsetof(Data, m_oathFulfilledZFactor) },
	{ "LargeUnitHeightFactor", INI::parseReal, nullptr, (int)offsetof(Data, m_largeUnitHeightFactor) },
	{ "LargeUnitMinHeight", INI::parseReal, nullptr, (int)offsetof(Data, m_largeUnitMinHeight) },
	{ "LargeUnitMaxHeight", INI::parseReal, nullptr, (int)offsetof(Data, m_largeUnitMaxHeight) },
	{ "LargeUnitTimeout", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_largeUnitTimeout) },
	{ "LargeUnitTailOff", INI::parseReal, nullptr, (int)offsetof(Data, m_largeUnitTailOff) },
	{ "ScatterSpeedFactor", INI::parseReal, nullptr, (int)offsetof(Data, m_scatterSpeedFactor) },
	{ "ScatterRandomness", INI::parseReal, nullptr, (int)offsetof(Data, m_scatterRandomness) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0x644FD0 (two SSE loops)
float normalizeAngle(float a)
{
	const float pi = 3.14159274f, twoPi = 6.28318548f;
	if (a > pi)
	{
		do
		{
			a = SimMath::subf32(a, twoPi);
		} while (a > pi);
	}
	if (-pi < a)
	{
		return a;
	}
	do
	{
		a = SimMath::addf32(a, twoPi);
	} while (-pi >= a);
	return a;
}

// RW 0x403111 stored as a float (fstp dword)
float length3f(float x, float y, float z)
{
	return SimMath::fstpDword(SimMath::length3d(x, y, z));
}

// RW 0x403175: the vector divided by its length (1 / length in SSE), unchanged when the length is 0
void normalize3(Coord3D &v)
{
	const float len = length3f(v.x, v.y, v.z);
	if (len == 0.0f)
	{
		return;
	}
	const float inv = SimMath::divf32(1.0f, len);
	v.x = SimMath::mulf32(v.x, inv);
	v.y = SimMath::mulf32(v.y, inv);
	v.z = SimMath::mulf32(v.z, inv);
}

// RW 0x403312 (Coord2D::normalize over RW 0x4032DD)
void normalize2(float &x, float &y)
{
	const float len = SimMath::length2d(x, y);
	if (len == 0.0f)
	{
		return;
	}
	const float inv = SimMath::divf32(1.0f, len);
	x = SimMath::mulf32(inv, x);
	y = SimMath::mulf32(y, inv);
}

// RW 0x405406 Coord2D::toAngle, the register result: acos of x / length clamped to [-1, 1] (SSE), negated when y < 0; 0 for a zero vector
double toAngleWide(float x, float y)
{
	const float len = SimMath::length2d(x, y);
	if (len == 0.0f)
	{
		return 0.0;
	}
	float c = SimMath::divf32(x, len);
	if (-1.0f > c)
	{
		c = -1.0f;
	}
	else if (c > 1.0f)
	{
		c = 1.0f;
	}
	const double a = SimMath::acosDet((double)c);
	return 0.0f > y ? -a : a;
}

float clamp01(float v)
{
	if (0.0f > v)
	{
		v = 0.0f;
	}
	if (v > 1.0f)
	{
		v = 1.0f;
	}
	return v;
}
} // namespace

void AODHordeContainBehaviorData::buildFieldParse(MultiIniFieldParse &p)
{
	HordeContainBehaviorData::buildFieldParse(p); // RW 0x878B63
	p.add(kFields);
}

void AODHordeContain::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "AODHordeContain";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<AODHordeContain>(thing, typed);
	});
}

AODHordeContain::AODHordeContain(Thing *thing, const Data *data)
	: HordeContain(thing, data)
	, m_aod(data)
{
	m_core->setFillInEnabled(false); // module slot 0x94: RW 0x9188EB
}

// RW 0x87E8FC
void AODHordeContain::onObjectCreated()
{
	HordeContain::onObjectCreated(); // RW 0x877751
	Object *horde = getObject();
	GameLogic &logic = horde->logic();
	const std::vector<HordeContainCore::Slot> &slots = m_core->slots();
	const int n = (int)slots.size();
	m_sway.reserve((size_t)n); // RW 0x87E5CA
	for (int i = 0; i < n; ++i)
	{
		// x87: FrequencyRandomness * 2 pi (fmul dword RW 0xBDD38C), stored; the bounds 1 +- r in SSE
		const float phaseRange = SimMath::fstpDword(SimMath::pc24MulW(m_aod->m_frequencyRandomness, kTwoPi));
		std::array<float, 6> r{};
		r[1] = logic.random().getValueReal(SimMath::subf32(1.0f, m_aod->m_amplitudeRandomness), SimMath::addf32(m_aod->m_amplitudeRandomness, 1.0f), kFile, 0x20A);
		r[0] = logic.random().getValueReal(0.0f, phaseRange, kFile, 0x20B);
		r[2] = logic.random().getValueReal(SimMath::subf32(1.0f, m_aod->m_frequencyRandomness), SimMath::addf32(m_aod->m_frequencyRandomness, 1.0f), kFile, 0x20C);
		const float phaseRangeZ = SimMath::fstpDword(SimMath::pc24MulW(m_aod->m_frequencyRandomnessZ, kTwoPi));
		r[4] = logic.random().getValueReal(SimMath::subf32(1.0f, m_aod->m_amplitudeRandomnessZ), SimMath::addf32(m_aod->m_amplitudeRandomnessZ, 1.0f), kFile, 0x20F);
		r[3] = logic.random().getValueReal(0.0f, phaseRangeZ, kFile, 0x210);
		r[5] = logic.random().getValueReal(SimMath::subf32(1.0f, m_aod->m_frequencyRandomnessZ), SimMath::addf32(m_aod->m_frequencyRandomnessZ, 1.0f), kFile, 0x211);
		m_sway.push_back(r); // RW 0x4BD250
	}
	// the rank lines: the slots' distinct X in slot order (at most 20)
	const Coord3D &hp = *horde->getPosition();
	const float c = SimMath::cosf32(horde->getOrientation()), s = SimMath::sinf32(horde->getOrientation()); // RW 0x86BFB5
	float x0 = 0.0f;
	for (int i = 0; i < n; ++i)
	{
		const float x = slots[(size_t)i].x;
		bool known = false;
		for (int k = 0; k < m_rankCount; ++k)
		{
			if (x == m_ranks[(size_t)k].dist)
			{
				known = true;
				break;
			}
		}
		if (known)
		{
			continue;
		}
		if (m_rankCount >= kRankLines)
		{
			break;
		}
		RankLine &r = m_ranks[(size_t)m_rankCount];
		r.dist = x;
		if (m_rankCount == 0)
		{
			x0 = x;
		}
		const float ox = SimMath::subf32(x, x0);
		const float rx = SimMath::subf32(SimMath::mulf32(ox, c), SimMath::mulf32(0.0f, s));
		const float ry = SimMath::addf32(SimMath::mulf32(ox, s), SimMath::mulf32(0.0f, c));
		r.flag = true;
		r.pos = Coord3D{ SimMath::addf32(hp.x, rx), SimMath::addf32(hp.y, ry), hp.z };
		r.angle = horde->getOrientation();
		++m_rankCount;
	}
	for (int k = m_rankCount - 1; k >= 0; --k)
	{
		trailPush(m_ranks[(size_t)k].pos);
	}
}

// RW 0x87D9A1
void AODHordeContain::trailPush(const Coord3D &pos)
{
	int n = m_trailCount;
	if (n == kTrailSize)
	{
		n = kTrailSize - 1;
	}
	else
	{
		m_trailCount = n + 1;
	}
	for (int i = n; i > 0; --i)
	{
		m_trail[(size_t)i] = m_trail[(size_t)i - 1];
	}
	m_trail[0].pos = pos;
	m_trail[0].angle = getObject()->getOrientation(); // + 0x44
}

// RW 0x87D691
void AODHordeContain::updateRankLines()
{
	if (m_rankCount <= 0)
	{
		return;
	}
	const float front = m_ranks[0].dist;
	float walked = 0.0f;
	int i = 0;
	Coord3D dir{ -1.0f, 0.0f, 0.0f }; // RW 0xBD19DC
	float seg = 0.0f;
	for (int k = 0; k < m_rankCount; ++k)
	{
		RankLine &r = m_ranks[(size_t)k];
		float rem = SimMath::subf32(SimMath::subf32(front, r.dist), walked);
		for (;;)
		{
			if (i < m_trailCount - 1)
			{
				dir = m_trail[(size_t)i + 1].pos;
				dir.x = SimMath::subf32(dir.x, m_trail[(size_t)i].pos.x);
				dir.y = SimMath::subf32(dir.y, m_trail[(size_t)i].pos.y);
				dir.z = 0.0f;
				seg = length3f(dir.x, dir.y, dir.z);
			}
			else
			{
				seg = kNoSegment;
			}
			if (seg > rem)
			{
				break;
			}
			++i;
			rem = SimMath::subf32(rem, seg);
			walked = SimMath::addf32(seg, walked);
		}
		Coord3D unit{ dir.x, dir.y, 0.0f };
		normalize3(unit);
		const TrailPoint &t = m_trail[(size_t)i];
		r.pos.x = SimMath::addf32(t.pos.x, SimMath::mulf32(unit.x, rem));
		r.pos.y = SimMath::addf32(t.pos.y, SimMath::mulf32(unit.y, rem));
		r.pos.z = SimMath::addf32(t.pos.z, SimMath::mulf32(unit.z, rem));
		const float a0 = t.angle;
		const float a1 = i < m_trailCount - 1 ? m_trail[(size_t)i + 1].angle : a0;
		const float turn = normalizeAngle(SimMath::subf32(a1, a0));
		// x87: fld rem, fdiv seg, fmulp the turn, fadd a0, then normalised (RW 0x87D849 .. 0x87D857)
		const float target = normalizeAngle(SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(turn, SimMath::pc24DivW(rem, seg)), a0)));
		const float cur = r.angle;
		const float d = normalizeAngle(SimMath::subf32(target, cur));
		float next = target;
		if (d > kTurnStep)
		{
			next = SimMath::addf32(cur, kTurnStep);
		}
		else if (-kTurnStep > d)
		{
			next = SimMath::subf32(cur, kTurnStep);
		}
		r.angle = normalizeAngle(next);
	}
}

// RW 0x87D9F6
bool AODHordeContain::runMemberPass()
{
	Object *horde = getObject();
	const float perFrame = SimMath::divf32(1.0f, SimMath::sseFromInt32((int)LOGICFRAMES_PER_SECOND));
	float speed = SimMath::mulf32(perFrame, 100.0f); // RW 0xBD88D8
	float step = SimMath::mulf32(perFrame, 10.0f);   // RW 0xBD83D8
	AIUpdateInterface *ai = horde->getAIUpdateInterface();
	if (Locomotor *loco = ai ? ai->curLocomotor() : nullptr) // RW 0x68B31D
	{
		speed = loco->getMaxSpeedForCondition(ai->locomotorHost()); // RW 0x5E3F49
		step = loco->getMaxAcceleration(ai->locomotorHost());       // RW 0x5E40AD
	}
	const Coord3D &hp = *horde->getPosition();
	const double moved = SimMath::length3d(SimMath::subf32(hp.x, m_ranks[0].pos.x), SimMath::subf32(hp.y, m_ranks[0].pos.y), SimMath::subf32(hp.z, m_ranks[0].pos.z));
	if (moved > (double)step)
	{
		trailPush(hp);
	}
	updateRankLines();
	static const int kOath = CombatNames::modelCondition("OATH_FULLFILLED");
	if (horde->testModelCondition(kOath))
	{
		m_oathDistance = SimMath::addf32(m_oathDistance, speed);
	}
	return HordeContain::runMemberPass(); // RW 0x873FE8
}

int AODHordeContain::rankLineOf(float x) const
{
	int k = m_rankCount - 1;
	while (k > 0 && !(m_ranks[(size_t)k].dist == x))
	{
		--k;
	}
	return k < 0 ? 0 : k;
}

Coord3D AODHordeContain::memberPassSlot(Object &member, const HordeContainCore::Placement &owner, const HordeAIUpdate *meleeHai, float &slotAngle)
{
	if (m_meleeEngaged) // + 0x2A0: HordeContain's slot 7 (RW 0x877D89)
	{
		return HordeContain::memberPassSlot(member, owner, meleeHai, slotAngle);
	}
	const int slot = m_core->slotOf(member.getID());
	slotAngle = slot >= 0 && (size_t)slot < m_core->slots().size() ? m_core->slots()[(size_t)slot].angle : 0.0f;
	return intendedPosition(member);
}

Coord3D AODHordeContain::getMemberFormationPosition(const Object *member) const
{
	if (!member)
	{
		return *getObject()->getPosition();
	}
	if (m_meleeEngaged)
	{
		return HordeContain::getMemberFormationPosition(member);
	}
	return const_cast<AODHordeContain *>(this)->intendedPosition(*member); // retail's slot 7 writes the dirty byte and ends the LARGE unit's rise
}

// RW 0x87DEFB (the member's position)
Coord3D AODHordeContain::intendedPosition(const Object &member)
{
	Object *horde = getObject();
	GameLogic &logic = horde->logic();
	const std::vector<HordeContainCore::Slot> &slots = m_core->slots();
	const int slot = m_core->slotIndexOrZero(member.getID()); // RW 0x86DEAF
	if (slots.empty())
	{
		return *horde->getPosition();
	}
	const HordeContainCore::Slot &rec = slots[(size_t)slot];
	const RankLine &line = m_ranks[(size_t)rankLineOf(rec.x)];
	const float a = line.angle;
	Coord3D p = line.pos;
	p.x = SimMath::fstpDword(SimMath::pc24SubW(p.x, SimMath::pc24MulW(SimMath::sind(a), rec.y))); // RW 0xA3CF90, fmul, fsubr
	p.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::cosd(a), rec.y), p.y)); // RW 0xA3CF84, fmul, fadd
	if (!m_core->built() || (size_t)slot >= m_sway.size()) // + 0x198, the sway records
	{
		return p;
	}
	static const int kOath = CombatNames::modelCondition("OATH_FULLFILLED");
	AIUpdateInterface *ai = horde->getAIUpdateInterface();
	float speedFrac = 0.0f;
	if (Locomotor *loco = ai ? ai->curLocomotor() : nullptr) // AI + 0x1F0
	{
		speedFrac = SimMath::fstpDword(SimMath::pc24DivW(loco->speed(), loco->getMaxSpeedForCondition(ai->locomotorHost()))); // RW 0x5E36F7 / 0x5E3F49, fdivr
	}
	const std::array<float, 6> &sw = m_sway[(size_t)slot];
	const double frame = SimMath::fildU32(logic.getFrame());
	// across: sin(frame * FrequencyScale * frequency + phase) * amplitude * the scaled AmplitudeScale
	const float still = clamp01(m_aod->m_stillAmplitude);
	if (still > 0.01f) // RW 0xBE5600
	{
		m_dirty = true; // + 0x120
	}
	const double t = SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24MulW(frame, m_aod->m_frequencyScale), sw[2]), sw[0]);
	const float wave = SimMath::fstpDword(SimMath::pc24MulW(SimMath::sind((float)t), sw[1]));
	const float amp = SimMath::mulf32(SimMath::addf32(SimMath::mulf32(SimMath::subf32(1.0f, still), speedFrac), still), m_aod->m_amplitudeScale);
	const float lateral = SimMath::mulf32(amp, wave);
	// RW 0x86BFB5 turns (0, lateral) by the horde's angle; the result is not read: the length and the angle of (0, lateral) are (RW 0x4032DD, 0x405406)
	const float len = SimMath::length2d(0.0f, lateral);
	const float turned = SimMath::fstpDword(SimMath::pc24AddW(toAngleWide(0.0f, lateral), a));
	float lx = SimMath::fstpDword(SimMath::pc24MulW(SimMath::fstpDword(SimMath::cosd(turned)), len)); // RW 0x42F4E0
	float ly = SimMath::fstpDword(SimMath::pc24MulW(SimMath::sind(turned), len));                     // RW 0x42F4D0
	// up and down
	const float stillZ = clamp01(m_aod->m_stillAmplitudeZ);
	if (stillZ > 0.01f)
	{
		m_dirty = true;
	}
	const double tz = SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24MulW(frame, m_aod->m_frequencyScaleZ), sw[5]), sw[3]);
	const double waveZ = SimMath::pc24AddW(SimMath::pc24MulW(SimMath::sind((float)tz), sw[4]), 0.95f); // RW 0xBDAD74
	const double ampZ = SimMath::pc24MulW(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24SubW(1.0, stillZ), speedFrac), stillZ), m_aod->m_amplitudeScaleZ);
	const float zOff = SimMath::fstpDword(SimMath::pc24MulW(ampZ, waveZ));
	const bool oath = member.testModelCondition(kOath);
	if (oath)
	{
		// the slot's second offset turned by the horde's angle (RW 0x86D720), normalised (RW 0x403312), times the scatter
		const float c = SimMath::cosf32(horde->getOrientation()), s = SimMath::sinf32(horde->getOrientation());
		float ox = SimMath::subf32(SimMath::mulf32(rec.x2, c), SimMath::mulf32(rec.y2, s));
		float oy = SimMath::addf32(SimMath::mulf32(rec.x2, s), SimMath::mulf32(rec.y2, c));
		normalize2(ox, oy);
		const float k = SimMath::sseFromInt32(slot % 5);
		const float scatter = SimMath::addf32(
			SimMath::mulf32(SimMath::mulf32(SimMath::mulf32(k, m_aod->m_scatterRandomness), m_oathDistance), 0.25f),
			SimMath::mulf32(m_aod->m_scatterSpeedFactor, m_oathDistance));
		lx = SimMath::addf32(SimMath::mulf32(ox, scatter), lx);
		ly = SimMath::addf32(SimMath::mulf32(oy, scatter), ly);
	}
	p.x = SimMath::fstpDword(SimMath::pc24AddW(lx, p.x));
	p.y = SimMath::fstpDword(SimMath::pc24AddW(ly, p.y));
	// the ground: the water when the point is under water (TheTerrainLogic vslot 0x4C), else the horde's layer (vslot 0x1C, RW 0x68BBE0)
	float ground = p.z;
	if (ai)
	{
		float water = 0.0f;
		if (ai->locomotorHost().isUnderwater(p.x, p.y, &water))
		{
			ground = water;
		}
		else if (ai->world().mapReady())
		{
			ground = ai->world().pathfinder().getLayerHeight(p.x, p.y, ai->adapter().getLayer());
		}
	}
	float z = SimMath::addf32(ground, zOff);
	if (m_largeUnit != INVALID_ID)
	{
		float h = SimMath::mulf32(m_aod->m_largeUnitHeightFactor, m_largeUnitHeight);
		if (h > m_aod->m_largeUnitMaxHeight)
		{
			h = m_aod->m_largeUnitMaxHeight;
		}
		if (m_aod->m_largeUnitMinHeight > h)
		{
			h = m_aod->m_largeUnitMinHeight;
		}
		const UnsignedInt elapsed = logic.getFrame() - m_largeUnitFrame;
		const UnsignedInt timeout = m_aod->m_largeUnitTimeout;
		if (elapsed >= timeout)
		{
			m_largeUnit = INVALID_ID; // the rise is over; no oath rise this time (RW 0x87E352)
			return Coord3D{ p.x, p.y, z };
		}
		const UnsignedInt half = timeout >> 1;
		float fade = 1.0f;
		if (elapsed > half)
		{
			fade = SimMath::fstpDword(SimMath::pc24SubW(1.0, SimMath::pc24DivW(SimMath::fildU32(elapsed - half), (double)(std::int32_t)half)));
		}
		const float dx = SimMath::fstpDword(SimMath::pc24SubW(p.x, m_largeUnitPos.x));
		const float dy = SimMath::fstpDword(SimMath::pc24SubW(p.y, m_largeUnitPos.y));
		const float fx = SimMath::cosf32(horde->getOrientation()), fy = SimMath::sinf32(horde->getOrientation()); // RW 0x70B9E0
		const float nfy = SimMath::subf32(0.0f, fy);
		const float along = SimMath::fstpDword(SimMath::absD(SimMath::pc24AddW(SimMath::pc24MulW(dx, fx), SimMath::pc24MulW(dy, fy))));    // RW 0xA3CF8A
		const float across = SimMath::fstpDword(SimMath::absD(SimMath::pc24AddW(SimMath::pc24MulW(dy, fx), SimMath::pc24MulW(dx, nfy))));
		float q = SimMath::subf32(across, m_largeUnitRadius);
		if (0.0f > q)
		{
			q = 0.0f;
		}
		q = SimMath::addf32(q, along);
		const float reach = SimMath::mulf32(m_aod->m_largeUnitTailOff, h);
		if (reach > q)
		{
			const double u = SimMath::pc24DivW(q, reach);
			const float uf = SimMath::fstpDword(u);
			const double smooth = SimMath::pc24AddW(SimMath::pc24MulW(SimMath::sqrtPC24(u), uf), SimMath::pc24MulW(SimMath::pc24MulW(u, uf), SimMath::pc24SubW(1.0, uf)));
			z = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24MulW(SimMath::pc24SubW(1.0, smooth), h), fade), z));
		}
	}
	if (oath)
	{
		const float k2 = SimMath::sseFromInt32((slot + 2) % 5);
		const float rise = SimMath::mulf32(SimMath::mulf32(m_aod->m_scatterSpeedFactor, m_aod->m_oathFulfilledZFactor), m_oathDistance);
		const float spread = SimMath::mulf32(
			SimMath::mulf32(SimMath::mulf32(SimMath::mulf32(k2, m_aod->m_scatterRandomness), m_aod->m_oathFulfilledZFactor), m_oathDistance), 0.25f);
		z = SimMath::addf32(SimMath::addf32(spread, rise), z);
	}
	return Coord3D{ p.x, p.y, z };
}

// RW 0x87DB01
void AODHordeContain::memberOrder(Object &member, const Coord3D &goal, float orientation, bool force)
{
	static const int kJustBuilt = CombatNames::modelCondition("JUST_BUILT"), kMoving = CombatNames::modelCondition("MOVING"),
					 kOath = CombatNames::modelCondition("OATH_FULLFILLED");
	if (member.testModelCondition(kJustBuilt))
	{
		HordeContain::memberOrder(member, goal, orientation, force); // RW 0x877A7A
		if (member.testModelCondition(kJustBuilt))
		{
			member.setModelConditionState(kJustBuilt, false);
		}
		return;
	}
	AIUpdateInterface *a = member.getAIUpdateInterface();
	if (a && a->isStateActive()) // AI vslot 0x1BC
	{
		if (member.testModelCondition(kMoving))
		{
			member.setModelConditionState(kMoving, false);
		}
		return;
	}
	if (m_meleeEngaged) // + 0x2A0
	{
		moveHub(member, goal, orientation); // RW 0x87468B
		return;
	}
	Object *horde = getObject();
	const std::vector<HordeContainCore::Slot> &slots = m_core->slots();
	const int slot = m_core->slotIndexOrZero(member.getID());
	float angle = slots.empty() ? orientation : m_ranks[(size_t)rankLineOf(slots[(size_t)slot].x)].angle;
	float reach = 9999.0f;         // RW 0xC18414
	float turnRate = 3.14159274f;  // RW 0xBDD388
	Locomotor *loco = a ? a->curLocomotor() : nullptr;
	if (loco)
	{
		reach = SimMath::fstpDword(SimMath::pc24MulW(loco->getMaxSpeedForCondition(a->locomotorHost()), 1.25f)); // RW 0xC2E434
		turnRate = loco->getMaxTurnRate(a->locomotorHost());                                                     // RW 0x5E372D
	}
	member.setModelConditionState(kMoving, horde->testModelCondition(kMoving)); // + 0x110 bit 29 of the horde
	if (horde->testModelCondition(kOath))
	{
		member.setModelConditionState(kOath, true);
		float rel = emotionRelAngle(member, goal); // RW 0x4B3D8D
		if (rel > turnRate)
		{
			rel = turnRate;
		}
		else if (SimMath::subf32(0.0f, turnRate) > rel)
		{
			rel = SimMath::subf32(0.0f, turnRate);
		}
		angle = SimMath::addf32(member.getOrientation(), rel);
	}
	else if (member.testModelCondition(kOath))
	{
		member.setModelConditionState(kOath, false);
	}
	const Coord3D &mp = *member.getPosition();
	Coord3D pos = mp;
	Coord3D d{ SimMath::subf32(goal.x, mp.x), SimMath::subf32(goal.y, mp.y), SimMath::subf32(goal.z, mp.z) };
	// RW 0x4054F5: x87 (z * z + y * y) + x * x, CRT sqrt, compared unrounded
	const double dist = SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(d.z, d.z), SimMath::pc24MulW(d.y, d.y)), SimMath::pc24MulW(d.x, d.x)));
	if (dist > (double)reach)
	{
		normalize3(d);
		pos.x = SimMath::addf32(SimMath::mulf32(d.x, reach), pos.x);
		pos.y = SimMath::addf32(SimMath::mulf32(d.y, reach), pos.y);
		pos.z = SimMath::addf32(SimMath::mulf32(d.z, reach), pos.z);
	}
	else
	{
		pos = goal;
	}
	// the rotation about Z by `angle` (CRT cos / sin, RW 0xA3CF84 / 0xA3CF90) and the position (RW 0x70BA76, then RW 0x70C201)
	const float c = SimMath::fstpDword(SimMath::cosd(angle)), s = SimMath::fstpDword(SimMath::sind(angle));
	const float basis[9] = { c, SimMath::subf32(0.0f, s), 0.0f, s, c, 0.0f, 0.0f, 0.0f, 1.0f };
	member.setTransform(&pos, basis);
	member.setPosition(&pos);
	if (a)
	{
		a->locomotorHost().setPendingPosition(pos); // + 0x198, + 0x1A6 = 1
	}
	if (PhysicsBehavior *ph = member.physicsModule()) // + 0x264
	{
		ph->setStunned(false); // RW 0x792A69(0)
	}
}

// RW 0x87D649
void AODHordeContain::onMemberContaining(Object *obj)
{
	GameLogic &logic = getObject()->logic();
	if (ObjectClientHooks *client = logic.clientHooks())
	{
		client->fadeIn(*obj, LOGICFRAMES_PER_SECOND * 5); // RW 0x70E013 -> 0x670AA2 (the drawable)
	}
	if (ExperienceTracker *xp = obj->getExperienceTracker()) // + 0x26C
	{
		xp->gainLevel(true); // RW 0x79D7AD(1)
	}
}

// RW 0x87E894
void AODHordeContain::noteMemberContact(Object *, Object *other)
{
	if (!other || other->isEffectivelyDead() || AODCrushCollide::deadCollideSize(*other) != 2 || other->getID() == m_largeUnit)
	{
		return;
	}
	m_largeUnit = other->getID();
	refreshLargeUnit();
	m_largeUnitFrame = getObject()->logic().getFrame();
}

// RW 0x87E516
void AODHordeContain::refreshLargeUnit()
{
	const Object *o = getObject()->logic().findObjectByID(m_largeUnit); // RW 0x449681
	if (!o)
	{
		m_largeUnit = INVALID_ID;
		return;
	}
	m_largeUnitPos = *o->getPosition();
	m_largeUnitRadius = kLargeUnitRadius;
	m_largeUnitHeight = ObjectGeometry::maxHeightAbovePosition(o->geometryShapes()); // RW 0xAD1920 on its GeometryInfo
}

void AODHordeContain::crc(StateHasher &hasher) const
{
	HordeContain::crc(hasher);
	hasher.addU32(0xA0D10000u | (std::uint32_t)m_trailCount);
	hasher.addU32((std::uint32_t)m_rankCount);
	for (int i = 0; i < m_trailCount; ++i)
	{
		const TrailPoint &t = m_trail[(size_t)i];
		hasher.addFloat(t.pos.x);
		hasher.addFloat(t.pos.y);
		hasher.addFloat(t.pos.z);
		hasher.addFloat(t.angle);
	}
	for (int k = 0; k < m_rankCount; ++k)
	{
		const RankLine &r = m_ranks[(size_t)k];
		hasher.addFloat(r.dist);
		hasher.addFloat(r.angle);
		hasher.addFloat(r.pos.x);
		hasher.addFloat(r.pos.y);
		hasher.addFloat(r.pos.z);
	}
	hasher.addU32((std::uint32_t)m_sway.size());
	for (const std::array<float, 6> &s : m_sway)
	{
		for (float v : s)
		{
			hasher.addFloat(v);
		}
	}
	hasher.addU32(m_largeUnit);
	hasher.addU32(m_largeUnitFrame);
	hasher.addFloat(m_oathDistance);
}

// RW 0x87E567: version 1, HordeContain's, + 0x318, + 0x334, + 0x31C, + 0x320; then (OpenBFME) the trail, the rank lines, the sway records and the
// LARGE unit's radius and top, so a loaded game goes on as the saved one
void AODHordeContain::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	HordeContain::xfer(x);
	x.xferObjectID(&m_largeUnit);
	x.xferReal(&m_oathDistance);
	x.xferUnsignedInt(&m_largeUnitFrame);
	x.xferReal(&m_largeUnitPos.x);
	x.xferReal(&m_largeUnitPos.y);
	x.xferReal(&m_largeUnitPos.z);
	x.xferReal(&m_largeUnitRadius);
	x.xferReal(&m_largeUnitHeight);
	std::uint32_t tc = (std::uint32_t)m_trailCount, rc = (std::uint32_t)m_rankCount, sc = (std::uint32_t)m_sway.size();
	x.xferUnsignedInt(&tc);
	x.xferUnsignedInt(&rc);
	x.xferUnsignedInt(&sc);
	if (x.isLoading())
	{
		if (tc > (std::uint32_t)kTrailSize || rc > (std::uint32_t)kRankLines)
		{
			throw XferException("AODHordeContain: the trail or the rank lines are too long");
		}
		m_trailCount = (int)tc;
		m_rankCount = (int)rc;
		m_sway.assign(sc, std::array<float, 6>{});
	}
	for (int i = 0; i < m_trailCount; ++i)
	{
		TrailPoint &t = m_trail[(size_t)i];
		x.xferReal(&t.pos.x);
		x.xferReal(&t.pos.y);
		x.xferReal(&t.pos.z);
		x.xferReal(&t.angle);
	}
	for (int k = 0; k < m_rankCount; ++k)
	{
		RankLine &r = m_ranks[(size_t)k];
		x.xferReal(&r.dist);
		x.xferReal(&r.angle);
		x.xferReal(&r.pos.x);
		x.xferReal(&r.pos.y);
		x.xferReal(&r.pos.z);
		x.xferBool(&r.flag);
	}
	for (std::array<float, 6> &s : m_sway)
	{
		for (float &f : s)
		{
			x.xferReal(&f);
		}
	}
}

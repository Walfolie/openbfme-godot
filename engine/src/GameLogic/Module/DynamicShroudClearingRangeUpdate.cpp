// OpenBFME. GPL-3.0.
// See GameLogic/Module/DynamicShroudClearingRangeUpdate.h (fork SHROUDPING-1: RotWK 2.01 RW 0x88E6AF .. 0x88EC88; BFME2 decomp
// DynamicShroudClearingRangeUpdate.cpp).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/DynamicShroudClearingRangeUpdate.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/System/ShroudManager.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char kGridDecalStop[] = "[S-7177] DynamicShroudClearingRangeUpdate: the 30 grid decals (RW 0x88E87F / 0x88E8C7, RadiusDecal) are the client's and not drawn";

// RW 0xC62648
const FieldParse kFields[] = {
	{ "ChangeInterval", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DynamicShroudClearingRangeUpdateModuleData, m_changeInterval) },
	{ "GrowInterval", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DynamicShroudClearingRangeUpdateModuleData, m_growInterval) },
	{ "ShrinkDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DynamicShroudClearingRangeUpdateModuleData, m_shrinkDelay) },
	{ "ShrinkTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DynamicShroudClearingRangeUpdateModuleData, m_shrinkTime) },
	{ "GrowDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DynamicShroudClearingRangeUpdateModuleData, m_growDelay) },
	{ "GrowTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DynamicShroudClearingRangeUpdateModuleData, m_growTime) },
	{ "FinalVision", INI::parseReal, nullptr, (int)offsetof(DynamicShroudClearingRangeUpdateModuleData, m_finalVision) },
	{ "GridDecalTemplate", parseRadiusDecalTemplate, nullptr, (int)offsetof(DynamicShroudClearingRangeUpdateModuleData, m_gridDecalTemplate) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x88EA6A .. 0x88EA95: fild (+ 2^32 for the high bit, an unsigned duration), fstp dword, then max(1.0f, it) (fld1 / fcompi: 1 when 1 > it)
float framesAtLeastOne(unsigned frames)
{
	const float f = (float)frames;
	return 1.0f > f ? 1.0f : f;
}
} // namespace

void DynamicShroudClearingRangeUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void DynamicShroudClearingRangeUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "DynamicShroudClearingRangeUpdate";
	typedef DynamicShroudClearingRangeUpdateModuleData Data;
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<DynamicShroudClearingRangeUpdate>(thing, typed);
	});
}

// RW 0x88E6D8
DynamicShroudClearingRangeUpdate::DynamicShroudClearingRangeUpdate(Thing *thing, const DynamicShroudClearingRangeUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	m_stateCountDown = (int)(data->m_shrinkTime + data->m_shrinkDelay);
	m_totalFrames = m_stateCountDown < 1 ? 1 : m_stateCountDown; // RW 0x88E744: jl
	m_shrinkStartDeadline = (unsigned)m_stateCountDown - data->m_shrinkDelay;
	m_growStartDeadline = (unsigned)m_stateCountDown - data->m_growDelay;
	m_sustainDeadline = m_growStartDeadline - data->m_growTime;
	Object *object = getObject();
	GameLogic &logic = object->logic();
	m_doneForeverFrame = logic.getFrame() + (unsigned)m_stateCountDown; // TheGameLogic + 0x40
	if (object->getControllingPlayer()) // RW 0x68B678
	{
		// RW 0x68E4E2 (fstp dword); the grid decals' clear (RW 0x7327E5) is the client's
		ShroudManager *shroud = logic.shroud();
		m_nativeClearingRange = shroud ? shroud->visionRange(*object)
		                               : ShroudManager::templateVision(*static_cast<const ThingTemplate *>(object->getTemplate())->getFinalOverride()).shroudClearingRange;
	}
}

// RW 0x88E9B0
UpdateSleepTime DynamicShroudClearingRangeUpdate::update()
{
	if (m_state == SLEEPING)
	{
		return UPDATE_SLEEP_NONE;
	}
	Object *me = getObject();
	const DynamicShroudClearingRangeUpdateModuleData *md = m_data;
	const unsigned currentFrame = me->logic().getFrame();
	if (!m_decalsCreated)
	{
		if (!md->m_gridDecalTemplate.texture.empty()) // RW 0x732DE7 makes nothing without a Texture
		{
			me->logic().noteStop(kGridDecalStop);
		}
		m_decalsCreated = true; // RW 0x88E9F7
	}

	if (m_stateCountDown <= 0 || currentFrame > m_doneForeverFrame)
	{
		m_state = DONE_FOREVER;
	}
	else if ((unsigned)m_stateCountDown <= m_shrinkStartDeadline)
	{
		m_state = SHRINKING;
	}
	else if ((unsigned)m_stateCountDown <= m_sustainDeadline)
	{
		m_state = SUSTAINING;
	}
	else if ((unsigned)m_stateCountDown <= m_growStartDeadline)
	{
		m_state = GROWING;
	}

	switch (m_state)
	{
	case NOT_STARTED_YET: // RW 0x88EB21: the decals' animation only
		break;
	case GROWING: // RW 0x88EAC6
		m_currentClearingRange = SimMath::addf32(SimMath::divf32(m_nativeClearingRange, framesAtLeastOne(md->m_growTime)), m_currentClearingRange);
		if (m_currentClearingRange >= m_nativeClearingRange)
		{
			m_state = SUSTAINING;
		}
		break;
	case SUSTAINING: // RW 0x88EAB6
		m_currentClearingRange = m_nativeClearingRange;
		break;
	case SHRINKING: // RW 0x88EA67
		m_currentClearingRange = SimMath::subf32(m_currentClearingRange,
			SimMath::divf32(SimMath::subf32(m_nativeClearingRange, md->m_finalVision), framesAtLeastOne(md->m_shrinkTime)));
		break;
	case DONE_FOREVER: // RW 0x88EA54
		m_currentClearingRange = md->m_finalVision;
		break;
	default:
		break;
	}

	if (m_stateCountDown > 0)
	{
		--m_stateCountDown;
	}
	if (m_changeIntervalCountdown > 0)
	{
		--m_changeIntervalCountdown;
	}
	else
	{
		m_changeIntervalCountdown = m_state == GROWING ? md->m_growInterval : md->m_changeInterval;
		me->setShroudClearingRange(m_currentClearingRange); // RW 0x68C234
		if (m_state == DONE_FOREVER)
		{
			m_state = SLEEPING;
		}
	}
	return UPDATE_SLEEP_NONE;
}

// the xfer's fields (retail's light CRC stops after UpdateModule's; the state is not saved, so it is not hashed either)
void DynamicShroudClearingRangeUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32((std::uint32_t)m_stateCountDown);
	hasher.addU32((std::uint32_t)m_totalFrames);
	hasher.addU32(m_growStartDeadline);
	hasher.addU32(m_sustainDeadline);
	hasher.addU32(m_shrinkStartDeadline);
	hasher.addU32(m_doneForeverFrame);
	hasher.addU32(m_changeIntervalCountdown);
	hasher.addU32(m_decalsCreated ? 1u : 0u);
	hasher.addFloat(m_visionChangePerInterval);
	hasher.addFloat(m_nativeClearingRange);
	hasher.addFloat(m_currentClearingRange);
}

// RW 0x88EB76
void DynamicShroudClearingRangeUpdate::xfer(Xfer &x)
{
	xferUpdateModule(x); // RW 0x850C60
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferInt(&m_stateCountDown);
	x.xferInt(&m_totalFrames);
	x.xferUnsignedInt(&m_growStartDeadline);
	x.xferUnsignedInt(&m_sustainDeadline);
	x.xferUnsignedInt(&m_shrinkStartDeadline);
	x.xferUnsignedInt(&m_doneForeverFrame);
	x.xferUnsignedInt(&m_changeIntervalCountdown);
	x.xferBool(&m_decalsCreated);
	x.xferReal(&m_visionChangePerInterval);
	x.xferReal(&m_nativeClearingRange);
	x.xferReal(&m_currentClearingRange);
}

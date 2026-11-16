// OpenBFME. GPL-3.0.
// See GameLogic/Module/OathbreakerModules.h (fork lane AOD-1: RotWK 2.01 RW 0x8C0026 .. 0x8C046D, 0x8BCB05 .. 0x8BCD4F).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/OathbreakerModules.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>
#include <variant>

namespace
{
// RW 0x73A302 / 0x73A368: the name is stored (the FX and OCL stores answer by name at the call)
void parseName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

bool stunned(const Object &obj)
{
	const PhysicsBehavior *p = obj.physicsModule(); // Object + 0x264
	return p && p->isStunned();                     // + 0x5C
}

using CrushData = AODCrushCollideModuleData;
using FadeData = OathbreakersFadeAwayBehaviorModuleData;

// RW 0xC710A8
const FieldParse kCrushFields[] = {
	{ "SmallFXList", parseName, nullptr, (int)offsetof(CrushData, m_fx[0]) },
	{ "SmallObjectCreationList", parseName, nullptr, (int)offsetof(CrushData, m_ocl[0]) },
	{ "MediumFXList", parseName, nullptr, (int)offsetof(CrushData, m_fx[1]) },
	{ "MediumObjectCreationList", parseName, nullptr, (int)offsetof(CrushData, m_ocl[1]) },
	{ "LargeFXList", parseName, nullptr, (int)offsetof(CrushData, m_fx[2]) },
	{ "LargeObjectCreationList", parseName, nullptr, (int)offsetof(CrushData, m_ocl[2]) },
	{ "Damage", INI::parseReal, nullptr, (int)offsetof(CrushData, m_damage) },
	{ "DamageType", INI::parseIndexList, TheDamageNames, (int)offsetof(CrushData, m_damageType) },
	{ "DeathType", INI::parseIndexList, TheWeaponDeathNames, (int)offsetof(CrushData, m_deathType) },
	{ "SpecialDamage", INI::parseReal, nullptr, (int)offsetof(CrushData, m_specialDamage) },
	{ "SpecialDamageType", INI::parseIndexList, TheDamageNames, (int)offsetof(CrushData, m_specialDamageType) },
	{ "SpecialDeathType", INI::parseIndexList, TheWeaponDeathNames, (int)offsetof(CrushData, m_specialDeathType) },
	{ "SelfDamage", INI::parseReal, nullptr, (int)offsetof(CrushData, m_selfDamage) },
	{ "SelfDamageType", INI::parseIndexList, TheDamageNames, (int)offsetof(CrushData, m_selfDamageType) },
	{ "SelfDeathType", INI::parseIndexList, TheWeaponDeathNames, (int)offsetof(CrushData, m_selfDeathType) },
	{ "SpecialObject", ParseObjectFilter, nullptr, (int)offsetof(CrushData, m_specialObject) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xC6FF84
const FieldParse kFadeFields[] = {
	{ "FadeOutTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(FadeData, m_fadeOutTime) },
	{ nullptr, nullptr, nullptr, 0 },
};

template <class Module, class Data>
void bind(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Module>(thing, typed);
	});
}
} // namespace

// ---- AODCrushCollide ---------------------------------------------------------------------------------------------------------------------------------

void AODCrushCollideModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kCrushFields);
}

void AODCrushCollide::registerModule(ModuleFactory &modules)
{
	bind<AODCrushCollide, CrushData>(modules, "AODCrushCollide");
}

AODCrushCollide::AODCrushCollide(Thing *thing, const CrushData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

int AODCrushCollide::deadCollideSize(const Object &obj)
{
	// the object table's row (RotWK: parseIndexList RW 0x42E976 over SMALL / MEDIUM / LARGE, RW 0xDA3D9C) stores the index; a BFME1 table keeps the token
	const FieldValue *v = obj.getTemplate()->findField("DeadCollideSize");
	if (!v)
	{
		return 0;
	}
	if (const long long *n = std::get_if<long long>(v))
	{
		return (int)(signed char)*n; // movsx byte (RW 0x8C03E0)
	}
	const RawTokens *raw = std::get_if<RawTokens>(v);
	if (!raw || raw->tokens.empty())
	{
		return 0;
	}
	static const char *const kNames[] = { "SMALL", "MEDIUM", "LARGE" };
	for (int i = 0; i < 3; ++i)
	{
		if (AsciiStringUtil::compareNoCase(raw->tokens[0], kNames[i]) == 0)
		{
			return i;
		}
	}
	throw std::runtime_error("DeadCollideSize: unknown value '" + raw->tokens[0] + "' in " + obj.getTemplate()->getName());
}

// RW 0x8C042F
UpdateSleepTime AODCrushCollide::update()
{
	if (!m_active)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	Object *obj = getObject();
	if (m_frame < obj->logic().getFrame())
	{
		static const int kFiringA = CombatNames::modelCondition("FIRING_A"); // + 0x111 bit 1 (41)
		obj->setModelConditionState(kFiringA, false);                      // RW 0x68B53C when it changed
		m_active = false;
	}
	return UPDATE_SLEEP_NONE;
}

// RW 0x8C0201
void AODCrushCollide::onCollide(Object *other, const Coord3D *, const Coord3D *)
{
	if (!other || stunned(*other))
	{
		return;
	}
	Object *self = getObject();
	static const int kOath = CombatNames::modelCondition("OATH_FULLFILLED"), kFiringA = CombatNames::modelCondition("FIRING_A");
	if (self->isEffectivelyDead() || self->testModelCondition(kOath) || stunned(*self)) // + 0x458 bit 0, RW 0x46E918(0xD7)
	{
		return;
	}
	if (other->getRelationship(*self) == ALLIES) // RW 0x68D7AB
	{
		return;
	}
	if (Object *container = self->getContainedBy()) // + 0x27C, its contain + 0x258 slot 0x7C
	{
		if (ContainModuleInterface *contain = container->getContain())
		{
			if (HordeContainInterface *horde = contain->getHordeContainInterface())
			{
				horde->noteMemberContact(self, other); // slot 0x130
			}
		}
	}
	if (other->isEffectivelyDead() || !other->physicsModule() || stunned(*other))
	{
		return;
	}
	GameLogic &logic = self->logic();
	const UnsignedInt until = logic.getFrame() + 2 * LOGICFRAMES_PER_SECOND; // RW 0xD9F608
	if (!m_active)
	{
		m_active = true;
		self->setModelConditionState(kFiringA, true);
		logic.friend_awakenUpdateModule(self, this, until); // RW 0x62B921
		m_frame = until;
	}
	m_frame = until;
	const bool special = ObjectFilterMatch::allows(logic, m_data->m_specialObject, *other, self->getControllingPlayer()); // RW 0x7640C1, RW 0x68B678
	DamageInfo hit;                                                                                                       // RW 0x66365E
	hit.m_input.m_damageType = special ? m_data->m_specialDamageType : m_data->m_damageType;
	hit.m_input.m_deathType = special ? m_data->m_specialDeathType : m_data->m_deathType;
	hit.m_input.m_amount = special ? m_data->m_specialDamage : m_data->m_damage;
	hit.m_input.m_sourceID = self->getID();
	other->attemptDamage(hit); // RW 0x698E7D
	m_lastVictim = other->getID();
	if (special)
	{
		DamageInfo back;
		back.m_input.m_damageType = m_data->m_selfDamageType;
		back.m_input.m_deathType = m_data->m_selfDeathType;
		back.m_input.m_amount = m_data->m_selfDamage;
		back.m_input.m_sourceID = other->getID();
		self->attemptDamage(back);
	}
	const int size = deadCollideSize(*other); // template + 0x605: 0, 1, else the large pair
	const std::string &fx = m_data->m_fx[size == 0 ? 0 : size == 1 ? 1 : 2];
	const std::string &ocl = m_data->m_ocl[size == 0 ? 0 : size == 1 ? 1 : 2];
	if (FXEventLog::isFXName(fx))
	{
		FXEvent e = FXEventLog::objectEvent(FXEvent::OBJECT_FX, "AODCrushCollide", logic.getFrame(), fx, *other); // RW 0x4B1B5A(fx, other, self)
		e.secondary = self->getID();
		logic.fxEvents().emit(e);
	}
	if (!ocl.empty() && AsciiStringUtil::compareNoCase(ocl, "None") != 0)
	{
		if (const ObjectCreationList *list = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(ocl) : nullptr)
		{
			list->create(logic, other, *self->getPosition()); // RW 0x5F0126(other, self, 0); INFERENCE: at this object's position (S-530)
		}
	}
}

void AODCrushCollide::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	if (m_active || m_lastVictim != INVALID_ID)
	{
		hasher.addU32(0xA0DC0000u | (m_active ? 1u : 0u));
		hasher.addU32(m_frame);
		hasher.addU32(m_lastVictim);
	}
}

// RW 0x8C004C: version 1, UpdateModule's, + 0x24, + 0x28, + 0x2C
void AODCrushCollide::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_frame);
	x.xferObjectID(&m_lastVictim);
	x.xferBool(&m_active);
}

// ---- OathbreakersFadeAwayBehavior -----------------------------------------------------------------------------------------------------------------------

void OathbreakersFadeAwayBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFadeFields);
}

void OathbreakersFadeAwayBehavior::registerModule(ModuleFactory &modules)
{
	bind<OathbreakersFadeAwayBehavior, FadeData>(modules, "OathbreakersFadeAwayBehavior");
}

OathbreakersFadeAwayBehavior::OathbreakersFadeAwayBehavior(Thing *thing, const FadeData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

// RW 0x8BCC50
void OathbreakersFadeAwayBehavior::start()
{
	Object *obj = getObject();
	static const unsigned kHordeMember = (unsigned)CombatNames::status("HORDE_MEMBER"); // 0x26
	if (obj->testStatus(kHordeMember))
	{
		return;
	}
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface()) // + 0x260
	{
		ai->aiBusyFor(0, CMD_FROM_AI); // AI + 0x20 RW 0x852E2A(0, 2)
		obj->setScriptSelectable(false); // RW 0x68BE3C(0)
	}
}

// RW 0x8BCC22
UpdateSleepTime OathbreakersFadeAwayBehavior::sleep() const
{
	const Object *obj = getObject();
	if (obj->isEffectivelyDead())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	static const int kOath = CombatNames::modelCondition("OATH_FULLFILLED");
	if (obj->testModelCondition(kOath))
	{
		return UPDATE_SLEEP_NONE;
	}
	return UPDATE_SLEEP(LOGICFRAMES_PER_SECOND); // RW 0xD9F608
}

// RW 0x8BCC82
UpdateSleepTime OathbreakersFadeAwayBehavior::update()
{
	Object *obj = getObject();
	static const int kOath = CombatNames::modelCondition("OATH_FULLFILLED");
	if (!obj->testModelCondition(kOath))
	{
		return sleep();
	}
	if (!m_started)
	{
		start();
		m_started = true;
	}
	const UnsignedInt frames = m_data->m_fadeOutTime ? m_data->m_fadeOutTime : 1u;
	// fild (unsigned: + 2^32 when negative), fdivr 1.0, fsubr opacity: on the x87 at the module's precision (RW 0x8BCCC2 .. 0x8BCCD7)
	m_opacity = SimMath::pc24Sub(m_opacity, SimMath::pc24Div(1.0f, (float)frames));
	GameLogic &logic = obj->logic();
	if (m_opacity < 0.0f)
	{
		m_opacity = 0.0f;
		if (Object *producer = logic.findObjectByID(obj->getProducerID())) // Object + 0x78, RW 0x449681
		{
			static const int kHorde = CombatNames::kindOf("HORDE"); // template + 0x115 bit 5
			if (producer->isKindOf((unsigned)kHorde))
			{
				if (ContainModuleInterface *contain = producer->getContain()) // + 0x258 slot 0x7C, the horde interface's slot 0x34 (RW 0x87370B)
				{
					if (contain->getHordeContainInterface())
					{
						contain->removeFromContain(obj);
					}
				}
			}
		}
		logic.destroyObject(obj); // RW 0x62BBAB
		return sleep();
	}
	obj->setLogicDrawableOpacity(m_opacity); // Drawable + 0xB0 (RW 0x70E013: the object's drawable)
	return sleep();
}

void OathbreakersFadeAwayBehavior::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	if (m_started)
	{
		hasher.addU32(0x0A7F0000u);
		hasher.addFloat(m_opacity);
	}
}

// RW 0x8BCB69: version 3; UpdateModule's from 2; the opacity from 3; the started byte
void OathbreakersFadeAwayBehavior::xfer(Xfer &x)
{
	XferVersion v{ 1, 3 };
	x.xferVersion(v);
	if (v.current >= 2)
	{
		xferUpdateModule(x);
	}
	if (v.current >= 3)
	{
		x.xferReal(&m_opacity);
	}
	x.xferBool(&m_started);
}

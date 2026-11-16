// OpenBFME. GPL-3.0.
//
// The Army of the Dead's own modules (fork lane AOD-1, part 1): AODCrushCollide and OathbreakersFadeAwayBehavior. RotWK 2.01 data: the Oathbreakers
// (RohanOathbreaker1 / RohanOathbreakersCavalry: AODCrushCollide; RohanOathbreakerHorde / RohanOathbreakerHordeSmall and their members:
// OathbreakersFadeAwayBehavior), made by GondorArmyofTheDeadEgg (Aragorn's OCL_GondorArmyofTheDeadEggSmall). Read from RotWK's game.dat (md5 daed3668,
// caveat S-001); the BFME2 decomp's AODCrushCollide.cpp was the reading aid (BFME2 1.06 VA + 0x4149 here):
//
// AODCrushCollide (an UpdateModule with a collide interface; collide vtable RW 0xC70EF4: onCollide RW 0x8C0201, wouldLikeToCollideWith RW 0x7FEAC1):
//   * the data (table RW 0xC710A8): SmallFXList + 0x08, SmallObjectCreationList + 0x0C, MediumFXList + 0x10, MediumObjectCreationList + 0x14, LargeFXList
//     + 0x18, LargeObjectCreationList + 0x1C (RW 0x73A302 / 0x73A368), Damage + 0x28 (0), DamageType + 0x20 (1, CRUSH), DeathType + 0x24 (0, NORMAL),
//     SpecialObject + 0x2C (an ObjectFilter, RW 0x76392F), SpecialDamage + 0x38, SpecialDamageType + 0x30 (1), SpecialDeathType + 0x34, SelfDamage + 0x44,
//     SelfDamageType + 0x3C (1), SelfDeathType + 0x40;
//   * the module: + 0x24 the frame the attack look ends, + 0x28 the last victim, + 0x2C active; xfer RW 0x8C004C: version 1, UpdateModule's, the
//     three fields;
//   * update RW 0x8C042F: while active, once the frame has passed, model condition FIRING_A (41) is cleared and the module is no longer active (it runs
//     every frame while active, else sleeps forever);
//   * onCollide RW 0x8C0201: nothing when the other object is stunned (its PhysicsBehavior + 0x5C), this object is effectively dead, has model
//     condition OATH_FULLFILLED (215) or is stunned, or the other's relationship to this one is ALLIES (RW 0x68D7AB). Then this object's container's
//     horde interface (contain slot 0x7C) slot 0x130 hears of the contact (AODHordeContain RW 0x87E894: a LARGE object to flow up over; every other
//     horde: nothing). Nothing more when the other is effectively dead, has no PhysicsBehavior or is stunned. Else FIRING_A for 2 seconds (set when
//     not active, the update woken for that frame, RW 0x62B921), one hit on the other (DamageInfo RW 0x66365E, attemptDamage RW 0x698E7D): the special
//     row when SpecialObject accepts it for this object's player (RW 0x7640C1), else the normal row, from this object; the other becomes the last
//     victim; a special hit also hits this object with the self row from the other. Last the FX and the OCL of the other's DeadCollideSize (template
//     + 0x605: SMALL, MEDIUM, else LARGE): doFXObj(fx, other, self) RW 0x4B1B5A, create(other, self) RW 0x5F0126.
//
// OathbreakersFadeAwayBehavior (an UpdateModule; registration RW 0x65806D: create RW 0x6506F8, 0x28 bytes, constructor RW 0x8BCBCB; data RW 0x65469C):
//   * the data: FadeOutTime + 0x08 (RW 0x73A429, ms to frames; the constructor's 1);
//   * the module: + 0x20 started, + 0x24 the opacity (1.0); xfer RW 0x8BCB69: version 3, UpdateModule's from 2, the opacity from 3, started;
//   * update RW 0x8BCC82: with model condition OATH_FULLFILLED: the first time (RW 0x8BCC50) an object that is not a HORDE_MEMBER (status 0x26) and has
//     an AI goes busy (AI + 0x20 RW 0x852E2A(0, 2)) and is no longer script selectable (RW 0x68BE3C(0)); then the opacity loses 1 / FadeOutTime (at
//     least 1 frame) each frame; below 0 it is 0, a member leaves its producer's horde (Object + 0x78; a HORDE: contain slot 0x7C slot 0x34, RW
//     0x87370B) and the object is destroyed (RW 0x62BBAB); else the drawable's opacity (Drawable + 0xB0) takes it. Sleep (RW 0x8BCC22): forever once
//     effectively dead, 1 frame with OATH_FULLFILLED, else LOGICFRAMES_PER_SECOND.
// INFERENCE: the OCL is made at this object's position (the port's create takes a position: S-530); the opacity reaches the drawable through the
// snapshot (Object::logicDrawableOpacity).

#pragma once

#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"

#include <string>

class ModuleFactory;
class StateHasher;

class AODCrushCollideModuleData : public ModuleData
{
public:
	std::string m_fx[3];  ///< + 0x08 / + 0x10 / + 0x18 (small, medium, large)
	std::string m_ocl[3]; ///< + 0x0C / + 0x14 / + 0x1C
	int m_damageType = 1;        ///< + 0x20
	int m_deathType = 0;         ///< + 0x24
	float m_damage = 0.0f;       ///< + 0x28
	ObjectFilter m_specialObject; ///< + 0x2C
	int m_specialDamageType = 1; ///< + 0x30
	int m_specialDeathType = 0;  ///< + 0x34
	float m_specialDamage = 0.0f; ///< + 0x38
	int m_selfDamageType = 1;    ///< + 0x3C
	int m_selfDeathType = 0;     ///< + 0x40
	float m_selfDamage = 0.0f;   ///< + 0x44
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AODCrushCollide : public UpdateModule, public CollideModuleInterface
{
public:
	OBFME_MODULE_XFER(AODCrushCollide)
	AODCrushCollide(Thing *thing, const AODCrushCollideModuleData *data);
	static void registerModule(ModuleFactory &modules);

	CollideModuleInterface *getCollide() override { return this; }
	UpdateSleepTime update() override;                                                  // RW 0x8C042F
	void onCollide(Object *other, const Coord3D *loc, const Coord3D *normal) override; // RW 0x8C0201
	bool wouldLikeToCollideWith(const Object &) const override { return false; }       // RW 0x7FEAC1
	void crc(StateHasher &hasher) const override;

	bool active() const { return m_active; }
	ObjectID lastVictim() const { return m_lastVictim; }
	UnsignedInt attackLookEnd() const { return m_frame; }

	// template + 0x605 (DeadCollideSize: SMALL 0, MEDIUM 1, LARGE 2; the template constructor's 0, RW 0x73FFAF)
	static int deadCollideSize(const Object &obj);

private:
	const AODCrushCollideModuleData *m_data;
	UnsignedInt m_frame = 0;         ///< + 0x24
	ObjectID m_lastVictim = INVALID_ID; ///< + 0x28
	bool m_active = false;           ///< + 0x2C
};

class OathbreakersFadeAwayBehaviorModuleData : public ModuleData
{
public:
	UnsignedInt m_fadeOutTime = 1; ///< + 0x08 (frames)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class OathbreakersFadeAwayBehavior : public UpdateModule
{
public:
	OBFME_MODULE_XFER(OathbreakersFadeAwayBehavior)
	OathbreakersFadeAwayBehavior(Thing *thing, const OathbreakersFadeAwayBehaviorModuleData *data); // RW 0x8BCBCB
	static void registerModule(ModuleFactory &modules);

	UpdateSleepTime update() override; // RW 0x8BCC82
	void crc(StateHasher &hasher) const override;

	bool started() const { return m_started; }
	float opacity() const { return m_opacity; }

private:
	void start();          // RW 0x8BCC50
	UpdateSleepTime sleep() const; // RW 0x8BCC22

	const OathbreakersFadeAwayBehaviorModuleData *m_data;
	bool m_started = false; ///< + 0x20
	float m_opacity = 1.0f; ///< + 0x24
};

// OpenBFME. GPL-3.0.
//
// HordeTransportContainDamage (fork HORDEDMG-1, package B of docs/fork/MODULE_CENSUS.md): the damage module of the Mumakil (RotWK 2.01 data: one
// template, MordorMumakil): when the beast is hurt, the archer horde on its back loses members in step with the beast's health. A BFME class; ZH has
// none. Read from RotWK's game.dat (md5 daed3668, caveat S-001):
//   * registration RW 0x65AE15 .. 0x65AE3A ("HordeTransportContainDamage", MODULETYPE_BEHAVIOR, interface mask 4: damage); the data RW 0x650C72 (0x8
//     bytes: a plain ModuleData, no field); the module RW 0x650C3A (0x14 bytes, constructor RW 0x8BF279: DamageModule's RW 0x8BD869, vtables RW
//     0xC70B28 / 0xC704B0 / 0xC70B1C); xfer (slot 3) is DamageModule's RW 0x8C651D: version 1, then RW 0x9933FE: no field of its own.
//   * onDamage RW 0x8BF2BF: nothing without a body (+ 0x25C) or a contain (+ 0x258). H = the body's health (slot 0x10), M = its maximum (slot 0x1C);
//     the health before the hit P = H + the hit's clipped damage (DamageInfo + 0x74), at most M (comiss / ja: M when the sum is not below M). For every
//     passenger of the contain's list (slot 0x118) in order that is a HORDE (template + 0x115 bit 5) with a horde interface (contain slot 0x7C, RW
//     0x872A6F) and a horde contain (horde slot 0x218, RW 0x872B02): n = the members with the ones entering (horde slot 0x180(0), RW 0x8706DF), C = the
//     Slots (horde slot 0x17C, RW 0x86C8DE); skipped while n <= 1 or C == 0. With k = 1 / (M / C) (x87 PC24: fild C; fdivr M; fld1; fdivrp, RW
//     0x8BF3E7 .. 0x8BF3F7) before = min(_ftol2(P * k) + 1, C) and after = min(_ftol2(k * H) + 1, C) (unsigned); nothing while n <= after. Else
//     before - after members die, at most n - after (RW 0x8BF41D .. 0x8BF42B, unsigned): the first ones of the horde contain's list (slot 0x118 of + 0x20),
//     an empty node counting as one. Their ids are collected over all passengers first (RW 0x5FFFBF), then each found object is killed (RW 0x698EC3(8,
//     0): UNRESISTABLE, NORMAL).

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DamageModule.h"

class ModuleFactory;

class HordeTransportContainDamage : public BehaviorModule, public DamageModuleInterface
{
public:
	OBFME_MODULE_XFER(HordeTransportContainDamage)
	HordeTransportContainDamage(Thing *thing, const ModuleData *data) : BehaviorModule(thing, data) {} // RW 0x8BF279
	DamageModuleInterface *getDamage() override { return this; }
	void onDamage(const DamageInfo &info) override;                                                // RW 0x8BF2BF
	void onBodyDamageStateChange(const DamageInfo *, BodyDamageType, BodyDamageType) override {} // RW 0x918BA0
	static void registerModule(ModuleFactory &modules);
};

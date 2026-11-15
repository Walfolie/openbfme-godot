// OpenBFME. GPL-3.0.
//
// AimWeaponBehavior (fork lane AIM-1): the update that tells the draw module where an aiming weapon points. RotWK 2.01 data: 23 uses in 12 templates (the
// archers of every faction, the Haradrim archer, the ents' rock throw ...); it changes model conditions only (AIM_HIGH / AIM_STRAIGHT / AIM_LOW,
// AIM_NEAR / AIM_FAR), which pick the aiming animations. Read from RotWK's game.dat (md5 daed3668, caveat S-001):
//   * registration RW 0x657DCA ("AimWeaponBehavior" RW 0xC0BD70, interface mask 1: update), the module's create RW 0x64ADAC (size 0x24), the data's RW 0x64ADE4;
//   * the data (RW 0x85E251, field table RW 0xC58178, parseReal RW 0x42ED00): AimLowThreshold + 0x08 (default -0.05), AimHighThreshold + 0x0C (0.05),
//     AimNearDistance + 0x10 (0), AimFarDistance + 0x14 (FLT_MAX);
//   * the module (constructor RW 0x85E29A over UpdateModule RW 0x653114, vtables RW 0xC580FC / 0xC580F0): + 0x20 "the aim conditions are set" (0), the
//     wake frame next frame (RW 0x850C32(1)); xfer RW 0x85E1C7: version 1, UpdateModule's (RW 0x850C60), + 0x20;
//   * update RW 0x85E30B: no AI (+ 0x260), drawable (RW 0x70E013) or current weapon (RW 0x68B58C(0)): sleep forever. Else "busy" = the AI's state is active
//     (vslot 0x1BC) or the weapon is between its pre-attack end and its follow-through end (RW 0x6CA2C3: + 0x1C <= now < + 0x24). With an aiming weapon
//     (template + 0x156 IsAimingWeapon) and busy: from the object's position (+ 0x38) to the target record (+ 0x3A8, RW 0x69213E's) the horizontal distance
//     d = sqrt(dy * dy + dx * dx) (x87 PC24, the CRT sqrt RW 0xA3CF96), at least 10 (RW 0xBD83D8), and the slope dz / d (divss): above AimHighThreshold
//     AIM_HIGH (178, + 0x120 bit 18), below AimLowThreshold AIM_LOW (180, bit 20), else AIM_STRAIGHT (179, bit 19): that bit is SET when clear (RW
//     0x68B53C), the others are not cleared; + 0x20 = 1. Then d below AimNearDistance: clear AIM_FAR, set AIM_NEAR (RW 0x68D607); above AimFarDistance:
//     clear AIM_NEAR, set AIM_FAR; between: neither. Not busy with + 0x20 set: the five conditions cleared (RW 0x5E3B79), + 0x20 = 0. Either change
//     refreshes the drawable at once (RW 0x67449C(0)). Returns 1 (every frame).

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/UpdateModule.h"

#include <cfloat>

class ModuleFactory;
class MultiIniFieldParse;

class AimWeaponBehaviorModuleData : public ModuleData
{
public:
	float m_aimLowThreshold = -0.05f;   ///< + 0x08 (RW 0xC29938)
	float m_aimHighThreshold = 0.05f;   ///< + 0x0C (RW 0xBDD760)
	float m_aimNearDistance = 0.0f;     ///< + 0x10
	float m_aimFarDistance = FLT_MAX;   ///< + 0x14 (RW 0xBD1910)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AimWeaponBehavior : public UpdateModule
{
public:
	OBFME_MODULE_XFER(AimWeaponBehavior)
	AimWeaponBehavior(Thing *thing, const AimWeaponBehaviorModuleData *data); // RW 0x85E29A
	UpdateSleepTime update() override;                                        // RW 0x85E30B
	void crc(StateHasher &hasher) const override;
	static void registerModule(ModuleFactory &modules);

	bool aimSet() const { return m_aimSet; } // + 0x20

private:
	const AimWeaponBehaviorModuleData *m_data;
	bool m_aimSet = false; ///< + 0x20
};

// OpenBFME. GPL-3.0.
//
// DestroyEnvironmentUpdate and PartTheHeavensUpdate (fork PARTHEAVENS-1, package B of docs/fork/MODULE_CENSUS.md): the two update modules of the
// object PartTheHeavens (RotWK 2.01 system.ini), which the OCL SUPERWEAPON_PartTheHeavens makes; GondorGandalf's OCLSpecialPower
// SuperweaponPartTheHeavens names it, but no command button of RotWK's data fires that power (a map script or the cinematic objects can). Donor: the
// BFME2 decomp's DestroyEnvironmentUpdate.cpp, PartTheHeavensUpdate.cpp, PartTheHeavensUpdateRebuild.cpp, PartTheHeavensUpdateRva004AC9FA.cpp and
// Rva004AC7C9FindModuleObject.cpp; read again from RotWK's game.dat (md5 daed3668, caveat S-001), the same statements:
//   * DestroyEnvironmentUpdate: registration RW 0x658F32 ("DestroyEnvironmentUpdate", RW 0xC0B6E8); data RW 0x64F160 (0x10 bytes, ctor RW 0x8AFC7F:
//     StartTime + 8 = 0, DestructionTime + 0xC = 100; table RW 0xC6B844, both parseDurationUnsignedInt RW 0x73A429); module RW 0x64F128 (0x28 bytes,
//     ctor RW 0x8AFCA4: the start frame + 0x20 = the logic frame, the environment object + 0x24 = 0).
//     update RW 0x8AFEB5: without an object yet, the first object of KindOf ENVIRONMENT (101; RW 0x8AFE33: ThePartitionManager's iterate RW 0xA39420
//     over the kind-of filter) with a module whose behavior interface slot 0x7C answers (RW 0x8AFC5F) is adopted; none: sleep forever. Only
//     RainOfFireUpdate answers that slot (its interface vtable RW 0xC6B6D0, the only one of the 128 vtables the binary stores at a module's + 0xC with slot 0x7C not RW 0x851E97; ctor
//     RW 0x8AF712, registration RW 0x658EFA), and no object of RotWK's data has a RainOfFireUpdate: the search finds none and the module sleeps
//     forever after its first update, as in retail. Past that (not reached on retail data): StartTime frames asleep, then over DestructionTime
//     frames the rain's slot 4 by -1 / DestructionTime each frame, then RW 0x8AFD8C (its slot 0 with 0, the object destroyed, the id 0; also the
//     destructor's). xfer RW 0x8AFD35: UpdateModule's (RW 0x850C60), version 1, the start frame, the object id.
//   * PartTheHeavensUpdate: registration RW 0x658F6A ("PartTheHeavensUpdate", RW 0xC0B6D0); data RW 0x64F1E9 (0x94 bytes, ctor RW 0x8B0273: Texture
//     + 8, Color + 0xC = 0, the FCurves Radius + 0x10 (0, 0), Opacity + 0x3C (0, 1), Angle + 0x68 (0, 0); table RW 0xC6B8F8: parseAsciiString
//     RW 0x42EE5E, parseColorInt RW 0x42F13E, the FCurve parse RW 0x73B723, Common/FCurve.h); module RW 0x64F1B1 (0x28 bytes, ctor RW 0x8B0013: the
//     start frame + 0x20 = the logic frame, the decal + 0x24 = 0).
//     update RW 0x8B022E: the frames since the start; at 0 the decal is made (RW 0x8B0155), then it is updated (RW 0x8B0075); sleeps none. Both are
//     the client's (TheProjectedShadowManager's decal of the Texture, its two children's size twice Radius, their angle +/- Angle in radians, the
//     opacity Opacity * 255): stop S-7176. xfer RW 0x8B0125: UpdateModule's, version 1, the start frame; loadPostProcess RW 0x8B0263 remakes the decal.

#pragma once

#include "Common/FCurve.h"
#include "Common/INI.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>

class ModuleFactory;

class DestroyEnvironmentUpdateModuleData : public ModuleData
{
public:
	unsigned m_startTime = 0;         ///< + 8 StartTime (frames)
	unsigned m_destructionTime = 100; ///< + 0xC DestructionTime (frames)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DestroyEnvironmentUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(DestroyEnvironmentUpdate)

	DestroyEnvironmentUpdate(Thing *thing, const DestroyEnvironmentUpdateModuleData *data); // RW 0x8AFCA4
	UpdateSleepTime update() override;                                                     // RW 0x8AFEB5
	void crc(StateHasher &hasher) const override;

	// tests
	const DestroyEnvironmentUpdateModuleData &data() const { return *m_data; }
	ObjectID environmentObject() const { return m_objectID; }
	unsigned startFrame() const { return m_startFrame; }

	static void registerModule(ModuleFactory &modules);

private:
	ObjectID findEnvironment() const; // RW 0x8AFE33

	const DestroyEnvironmentUpdateModuleData *m_data;
	unsigned m_startFrame = 0x3FFFFFFF; ///< + 0x20
	ObjectID m_objectID = INVALID_ID;   ///< + 0x24
};

class PartTheHeavensUpdateModuleData : public ModuleData
{
public:
	PartTheHeavensUpdateModuleData(); // RW 0x8B0273
	std::string m_texture; ///< + 8 Texture
	int m_color = 0;       ///< + 0xC Color
	FCurve m_radius;       ///< + 0x10 Radius
	FCurve m_opacity;      ///< + 0x3C Opacity
	FCurve m_angle;        ///< + 0x68 Angle
	static void buildFieldParse(MultiIniFieldParse &p);
};

class PartTheHeavensUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(PartTheHeavensUpdate)

	PartTheHeavensUpdate(Thing *thing, const PartTheHeavensUpdateModuleData *data); // RW 0x8B0013
	UpdateSleepTime update() override;                                             // RW 0x8B022E
	void crc(StateHasher &hasher) const override;

	// tests
	const PartTheHeavensUpdateModuleData &data() const { return *m_data; }
	unsigned startFrame() const { return m_startFrame; }

	static void registerModule(ModuleFactory &modules);

private:
	const PartTheHeavensUpdateModuleData *m_data;
	unsigned m_startFrame = 0x3FFFFFFF; ///< + 0x20
};

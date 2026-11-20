// OpenBFME. GPL-3.0.
// See GameLogic/Module/ExtraModules.h.

#include "GameLogic/Module/ExtraModules.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Module/CommandButtonHuntUpdate.h"
#include "GameLogic/Module/AimWeaponBehavior.h"
#include "GameLogic/Module/AudioLoopUpgrade.h"
#include "GameLogic/Module/ClickReactionBehavior.h"
#include "GameLogic/Module/CritterEmitterUpdate.h"
#include "GameLogic/Module/DamageFieldUpdate.h"
#include "GameLogic/Module/CastleModules2.h"
#include "GameLogic/Module/RunOffMapBehavior.h"
#include "GameLogic/Module/HordeTransportContainDamage.h"
#include "GameLogic/Module/SpecialEnemySenseUpdate.h"
#include "GameLogic/Module/StrafeAreaUpdate.h"
#include "GameLogic/Module/ProjectileStreamUpdate.h"
#include "GameLogic/Module/PartTheHeavensModules.h"
#include "GameLogic/Module/DynamicShroudClearingRangeUpdate.h"
#include "GameLogic/Module/OathbreakerModules.h"
#include "GameLogic/Object/Contain/AODHordeContain.h"
#include "GameLogic/Module/EvacuateDamage.h"
#include "GameLogic/Module/ExtraCreateModules.h"
#include "GameLogic/Module/ExtraDieModules.h"
#include "GameLogic/Module/ExtraUpdateModules.h"
#include "GameLogic/Module/ExtraUpgradeModules.h"
#include "GameLogic/Module/FireWeaponWhenDamagedBehavior.h"
#include "GameLogic/Module/FlingPassengerSpecialAbilityUpdate.h"
#include "GameLogic/Module/FlammableUpdate.h"
#include "GameLogic/Module/GeometryUpgrade.h"
#include "GameLogic/Module/RebuildHoleModules.h"
#include "GameLogic/Module/SlaveWatcherBehavior.h"
#include "GameLogic/Module/SpellRechargeModifierUpgrade.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace
{
template <class Runtime, class Data>
void bind(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}
} // namespace

void ExtraModules::registerAll(ModuleFactory &modules)
{
	bind<AttributeModifierUpgrade, AttributeModifierUpgradeModuleData>(modules, "AttributeModifierUpgrade");
	bind<AllowBannerSpawnUpgrade, UpgradeModuleData>(modules, "AllowBannerSpawnUpgrade"); // lane BANNER-1
	GeometryUpgrade::registerModule(modules); // lane MODCOMPAT-MOD1
	bind<CreateObjectDie, CreateObjectDieModuleData>(modules, "CreateObjectDie");
	bind<FireWeaponWhenDeadBehavior, FireWeaponWhenDeadBehaviorModuleData>(modules, "FireWeaponWhenDeadBehavior");
	FireWeaponWhenDamagedBehavior::registerModule(modules); // lane MODS-3
	EvacuateDamage::registerModule(modules);                // fork EVACUATE-1
	FlingPassengerSpecialAbilityUpdate::registerModule(modules); // fork FLING-1
	AimWeaponBehavior::registerModule(modules);             // fork AIM-1
	SpellRechargeModifierUpgrade::registerModule(modules);  // fork SPELLRECHARGE-1
	DamageFieldUpdate::registerModule(modules);             // fork DAMAGEFIELD-1
	AODCrushCollide::registerModule(modules);               // fork AOD-1
	OathbreakersFadeAwayBehavior::registerModule(modules);  // fork AOD-1
	AODHordeContain::registerModule(modules);               // fork AOD-1
	CastleModules2::registerAll(modules);                  // fork CASTLEMOD-1
	RunOffMapBehavior::registerModule(modules);             // fork RUNOFF-1
	HordeTransportContainDamage::registerModule(modules);   // fork HORDEDMG-1
	SpecialEnemySenseUpdate::registerModule(modules);       // fork SENSE-1
	StrafeAreaUpdate::registerModule(modules);              // fork STRAFE-1
	ProjectileStreamUpdate::registerModule(modules);        // fork STREAM-1
	DestroyEnvironmentUpdate::registerModule(modules);      // fork PARTHEAVENS-1
	PartTheHeavensUpdate::registerModule(modules);          // fork PARTHEAVENS-1
	DynamicShroudClearingRangeUpdate::registerModule(modules); // fork SHROUDPING-1
	CommandButtonHuntUpdate::registerModule(modules);       // lane MODCOMPAT-MOD1
	CritterEmitterUpdate::registerModule(modules); // lane MODCOMPAT-MOD1
	AudioLoopUpgrade::registerModule(modules);     // lane MODCOMPAT-MOD1
	ClickReactionBehavior::registerModule(modules); // lane MODCOMPAT-MOD1
	bind<UpgradeDie, UpgradeDieModuleData>(modules, "UpgradeDie");
	bind<HeroDie, HeroDieModuleData>(modules, "HeroDie");
	bind<DeletionUpdate, DeletionUpdateModuleData>(modules, "DeletionUpdate");
	bind<MonitorConditionUpdate, MonitorConditionUpdateModuleData>(modules, "MonitorConditionUpdate");
	bind<FlammableUpdate, FlammableUpdateModuleData>(modules, "FlammableUpdate");
	bind<FireSpreadUpdate, FireSpreadUpdateModuleData>(modules, "FireSpreadUpdate");
	bind<ExperienceLevelCreate, ExperienceLevelCreateModuleData>(modules, "ExperienceLevelCreate");
	bind<LockWeaponCreate, LockWeaponCreateModuleData>(modules, "LockWeaponCreate");
	bind<InheritUpgradeCreate, InheritUpgradeCreateModuleData>(modules, "InheritUpgradeCreate");
	RebuildHoleModules::registerModules(modules); // lane MODCOMPAT-MOD1: RebuildHoleExposeDie, RebuildHoleBehavior
	SlaveWatcherBehavior::registerModule(modules); // lane MODCOMPAT-MOD1
}

# Lane MODULES-1 (the remaining behaviour modules the base game uses): sources and tests of the openbfme_core library and the test executable. Included
# from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): every module here is object state that enters the lockstep hash
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/VeterancyCrateCollide.cpp
    src/GameLogic/Module/ExtraModules.cpp
    src/GameLogic/Module/ExtraUpgradeModules.cpp
    src/GameLogic/Module/ExtraDieModules.cpp
    src/GameLogic/Module/ExtraUpdateModules.cpp
    src/GameLogic/Module/ExtraCreateModules.cpp
    src/GameLogic/Module/FlammableUpdate.cpp
    src/GameLogic/Module/FireWeaponWhenDamagedBehavior.cpp # lane MODS-3
    src/GameLogic/Module/GeometryUpgrade.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/RebuildHoleModules.cpp # lane MODCOMPAT-MOD1: RebuildHoleExposeDie, RebuildHoleBehavior
    src/GameLogic/Module/SlaveWatcherBehavior.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/CommandButtonHuntUpdate.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/GrabPassengerSpecialPower.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/CritterEmitterUpdate.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/AudioLoopUpgrade.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/ClickReactionBehavior.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/EvacuateDamage.cpp # fork EVACUATE-1
    src/GameLogic/Module/FlingPassengerSpecialAbilityUpdate.cpp # fork FLING-1
    src/GameLogic/Module/AimWeaponBehavior.cpp # fork AIM-1
    src/GameLogic/Module/SpellRechargeModifierUpgrade.cpp # fork SPELLRECHARGE-1
    src/GameLogic/Module/DamageFieldUpdate.cpp # fork DAMAGEFIELD-1
    src/GameLogic/Module/OathbreakerModules.cpp # fork AOD-1
    src/GameLogic/Object/Contain/AODHordeContain.cpp # fork AOD-1
    src/GameLogic/Module/CastleModules2.cpp # fork CASTLEMOD-1
    src/GameLogic/Module/RunOffMapBehavior.cpp # fork RUNOFF-1
    src/GameLogic/Module/HordeTransportContainDamage.cpp # fork HORDEDMG-1
    src/GameLogic/Module/SpecialEnemySenseUpdate.cpp # fork SENSE-1
    src/GameLogic/Module/StrafeAreaUpdate.cpp # fork STRAFE-1
    src/GameLogic/Module/ProjectileStreamUpdate.cpp # fork STREAM-1
    src/GameLogic/Module/PartTheHeavensModules.cpp # fork PARTHEAVENS-1: DestroyEnvironmentUpdate, PartTheHeavensUpdate
    src/Common/FCurve.cpp # fork PARTHEAVENS-1: the FCurve field type
    src/GameLogic/FireLogicSystem.cpp # fork FIRELOGIC-1: TheFireLogicSystem (the terrain fire grid)
    src/GameLogic/Object/CollisionManager.cpp # fork COLLIDE-1: TheCollisionManager's library
    src/GameLogic/Object/ObjectCollision.cpp # fork COLLIDE-1: an Object's collidable
)
set(OPENBFME_MOD1_TESTS
    tests/test_module_coverage.cpp
    tests/test_mod1_retail.cpp
    tests/test_mod1_parse.cpp
    tests/test_fwwd.cpp # lane MODS-3: FireWeaponWhenDamagedBehavior (synthetic and retail data)
    tests/test_geometry_upgrade.cpp # lane MODCOMPAT-MOD1: GeometryUpgrade and the object's own geometry (retail data)
    tests/test_rebuildhole.cpp # lane MODCOMPAT-MOD1: RebuildHoleExposeDie / RebuildHoleBehavior (retail data, binary facts)
    tests/test_slavewatcher.cpp # lane MODCOMPAT-MOD1: SlaveWatcherBehavior and its readers (retail data, binary facts)
    tests/test_cbhunt.cpp # lane MODCOMPAT-MOD1: CommandButtonHuntUpdate and TEAM_HUNT_WITH_COMMAND_BUTTON
    tests/test_grabpassenger.cpp # lane MODCOMPAT-MOD1: GrabPassengerSpecialPower
    tests/test_critter_emitter.cpp # lane MODCOMPAT-MOD1: CritterEmitterUpdate (synthetic and retail data)
    tests/test_audio_loop_upgrade.cpp # lane MODCOMPAT-MOD1: AudioLoopUpgrade (synthetic and retail data)
    tests/test_click_reaction.cpp # lane MODCOMPAT-MOD1: ClickReactionBehavior (synthetic and retail data)
    tests/test_castle_unpack_script.cpp # lane CASTLE-UNPACK: the castle base script commands (NAMED_BASE_UNPACK*, PLAYER_HAS_N_OR_FEWER_BASES)
    tests/test_modcompat_ai_script.cpp # lane MODCOMPAT-AI: the script conditions and actions the mods' AI scripts need
    tests/test_evacuate_damage.cpp # fork EVACUATE-1: EvacuateDamage (synthetic data)
    tests/test_fling_passenger.cpp # fork FLING-1: FlingPassengerSpecialAbilityUpdate (synthetic data)
    tests/test_aim_weapon_behavior.cpp # fork AIM-1: AimWeaponBehavior (synthetic data)
    tests/test_spell_recharge_modifier.cpp # fork SPELLRECHARGE-1: SpellRechargeModifierUpgrade (synthetic data)
    tests/test_spawnunit_retail.cpp # fork SPAWNUNIT-1: SpawnUnitBehavior's update (retail data)
    tests/test_damage_field.cpp # fork DAMAGEFIELD-1: DamageFieldUpdate (synthetic data)
    tests/test_aod_modules.cpp # fork AOD-1: AODCrushCollide, OathbreakersFadeAwayBehavior (synthetic data)
    tests/test_aod_horde_retail.cpp # fork AOD-1: AODHordeContain (retail data)
    tests/test_castle_modules2.cpp # fork CASTLEMOD-1: GateProxyBehavior, OCLUpdate (synthetic data)
    tests/test_run_off_map.cpp # fork RUNOFF-1: RunOffMapBehavior, AI states 30 / 55 (synthetic data)
    tests/test_ship_slow_death.cpp # fork SHIPDEATH-1: ShipSlowDeathBehavior (synthetic data)
    tests/test_special_enemy_sense.cpp # fork SENSE-1: SpecialEnemySenseUpdate (synthetic data)
    tests/test_firelogic.cpp # fork FIRELOGIC-1: TheFireLogicSystem (synthetic data)
    tests/test_collision_manager.cpp # fork COLLIDE-1: the collision manager library (synthetic boxes)
)

# Unported logic modules: census and order (fork, 2026-11-16)

The simulation modules that are still unported (status `missing`, or `base-ai` for an AI class running as the base AIUpdateInterface) and used by
multiplayer objects. Counted by a scratch script over the installed data: RotWK 2.01 (`C:\Program Files (x86)\BFME\RotWK`) and Edain
(`C:\RotWK`, the `__edain_*` archives first). A template counts when it or a ChildObject parent declares the class; files whose path names a
cinematic, demo, obsolete, campaign, Create-a-Hero, WotR, tutorial or test set are left out (a few Edain campaign templates with other names still
count). Draw modules and client behaviours (`W3D*Draw`, `*ClientBehavior`, `*ClientUpdate`, EvaAnnounceClientCreate) are presentation and not listed.

| RotWK MP templates | Edain MP templates | Class | Package |
|---:|---:|---|---|
| 40 | 40 | ThreatFinderUpdate (the skirmish AI's base defence nodes) | C |
| 30 | 54 | AnimalAIUpdate (base AI) | E |
| 16 | 43 | RubbleRiseUpdate | A |
| 13 | 33 | ShipSlowDeathBehavior | B |
| 4 | 57 | TooltipUpgrade (tooltips) | E |
| 4 | 22 | WallUpgradeUpdate (castle walls) | A |
| 4 | 20 | AnnounceBirthAndDeathBehavior (announcements) | E |
| 3 | 13 | GiveUpgradeUpdate | B |
| 3 | 9 | RunOffMapBehavior | B |
| 3 | 3 | GateProxyBehavior | A |
| 3 | 3 | LaserUpdate (Gandalf's powers) | B |
| 3 | 1 | OCLUpdate (the castle wall catapults) | A |
| 2 | 47 | SpecialEnemySenseUpdate | B |
| 2 | 2 | CivilianSpawnCollide | E |
| 2 | 2 | ManTheWallsSpecialPower | A |
| 1 | 7 | HordeTransportContainDamage (mumakil) | B |
| 1 | 7 | StrafeAreaUpdate | B |
| 1 | 2 | ProjectileStreamUpdate | B |
| 1 | 2 | DestroyEnvironmentUpdate | B |
| 1 | 1 | PartTheHeavensUpdate, DynamicShroudClearingRangeUpdate, BridgeBehavior, BaseUpgrade, DelayedLuaEventUpdate, ToppleUpdate, FadeAndDieOrnamentUpdate, CivilianSpawnUpdate | B / E |
| 1 | 1 | HordeWorkerAIUpdate (base AI) | E |
| 0 | 39 | GiantBirdSlowDeathBehavior | D |
| 0 | 32 | SplitHordeSpecialPower | D |
| 0 | 31 | EntEnragedUpdate | D |
| 0 | 22 | FreeLifeBody | D |
| 0 | 18 | DeflectSpecialPower | D |
| 0 | 7 | ReplaceObjectUpdate, OilSpillUpdate, LocomotorSetUpgrade | D |
| 0 | 5 | OneRingPenaltyUpdate, TeleportToCasterSpecialPower | D |
| 0 | 4 | CombineHordeSpecialPower, StoreObjectsSpecialPower | D |
| 0 | 3 | ReplenishUnitsBehavior | D |
| 0 | 2 | SupplyWarehouseCreate, SupplyWarehouseDockUpdate, ReflectDamage | D |
| 0 | 1 | FireWeaponCollide, RainOfFireUpdate | D |

46 classes in all, 28 of them used by RotWK 2.01 multiplayer objects.

Packages (in this order; each package is ported, tested and committed together):
- **A, castles and walls**: WallUpgradeUpdate, GateProxyBehavior, OCLUpdate, ManTheWallsSpecialPower, RubbleRiseUpdate.
- **B, units, heroes and powers**: RunOffMapBehavior, HordeTransportContainDamage, ShipSlowDeathBehavior, SpecialEnemySenseUpdate, GiveUpgradeUpdate,
  LaserUpdate, StrafeAreaUpdate, ProjectileStreamUpdate, DestroyEnvironmentUpdate, PartTheHeavensUpdate, DynamicShroudClearingRangeUpdate,
  BridgeBehavior, BaseUpgrade.
- **C, skirmish AI**: ThreatFinderUpdate.
- **D, Edain only**: GiantBirdSlowDeathBehavior, SplitHordeSpecialPower, EntEnragedUpdate, FreeLifeBody, DeflectSpecialPower, then the small ones.
- **E, presentation or no multiplayer effect**: TooltipUpgrade, AnnounceBirthAndDeathBehavior, AnimalAIUpdate, HordeWorkerAIUpdate, the civilian
  modules, DelayedLuaEventUpdate, ToppleUpdate, FadeAndDieOrnamentUpdate.

Done before this census: fork lane AOD-1 (AODCrushCollide, OathbreakersFadeAwayBehavior, AODHordeContain).

Done since: package A, fork lane CASTLEMOD-1: GateProxyBehavior, OCLUpdate, RubbleRiseUpdate, WallUpgradeUpdate (without its move-out routine RW 0x8AEC79, S-1423), ManTheWallsSpecialPower: package A is done. Package B, fork lane RUNOFF-1: RunOffMapBehavior with the AI commands 0x38 / 0x41 and their states 30 / 55; fork lane HORDEDMG-1: HordeTransportContainDamage (the mumak loses archers with its health); fork lane SHIPDEATH-1: ShipSlowDeathBehavior (a sinking ship keels over).

# OpenMoHAA Complete Events Reference

This document lists ALL events available in the OpenMoHAA engine, organized by category.

**Total Events: 1,942 Internal Events + 67 ScriptDelegate Events**

---

## Table of Contents

1. [ScriptDelegate Events (Stats/Hooks)](#scriptdelegate-events-statshooks)
2. [Actor Events (255)](#actor-events)
3. [ScriptThread Events (234)](#scriptthread-events)
4. [Player Events (172)](#player-events)
5. [Weapon Events (89)](#weapon-events)
6. [Vehicle Events (87)](#vehicle-events)
7. [Sentient Events (64)](#sentient-events)
8. [ScriptSlave Events (63)](#scriptslave-events)
9. [Level Events (59)](#level-events)
10. [World Events (50)](#world-events)
11. [Projectile Events (40)](#projectile-events)
12. [Turret Events (35)](#turret-events)
13. [Camera Events (34)](#camera-events)
14. [Entity Events (30)](#entity-events)
15. [HUD Events (29)](#hud-events)
16. [ViewThing Events (27)](#viewthing-events)
17. [CameraManager Events (27)](#cameramanager-events)
18. [FuncBeam Events (25)](#funcbeam-events)
19. [Door Events (25)](#door-events)
20. [Trigger Events (24+)](#trigger-events)
21. [Projectile Generator Events (24)](#projectile-generator-events)
22. [UseObject Events (19)](#useobject-events)
23. [SoundManager Events (19)](#soundmanager-events)
24. [Item Events (18)](#item-events)
25. [VehicleTurretGun Events (16)](#vehicleturretgun-events)
26. [Miscellaneous Events](#miscellaneous-events)

---

## ScriptDelegate Events (Stats/Hooks)

These are the NEW subscribable events for stat tracking via `event_subscribe`.

### Combat Events
| Event | Description | Parameters |
|-------|-------------|------------|
| `player_kill` | Player killed another player | attacker, attacker, victim, inflictor, location, mod |
| `player_death` | Player died | player, inflictor |
| `player_damage` | Player took damage | player, attacker, damage, mod |
| `player_pain` | Player pain with hit location | player, attacker, damage, mod, location |
| `player_headshot` | Headshot kill | attacker, victim, weapon |
| `player_suicide` | Player suicide | player |
| `player_crushed` | Player crushed | player, attacker |
| `player_telefragged` | Player telefragged | player, attacker |
| `player_roadkill` | Vehicle kill | attacker, victim |
| `player_bash` | Melee bash kill | attacker, victim |
| `player_teamkill` | Team kill | killer, victim |
| `weapon_fire` | Weapon fired | owner, weapon_name, ammo_left |
| `weapon_hit` | Weapon hit target | owner, target, location/type |
| `weapon_reload` | Weapon reloading | owner, weapon_name |
| `weapon_reload_done` | Reload complete | owner, weapon_name |
| `weapon_change` | Weapon switched | owner, old_weapon, new_weapon, client |
| `weapon_ready` | Weapon ready | owner, weapon_name |
| `weapon_no_ammo` | Out of ammo | owner, weapon_name |
| `weapon_holster` | Weapon holstered | owner, weapon_name |
| `weapon_raise` | Weapon raised | owner, weapon_name |
| `weapon_drop` | Weapon dropped | owner, weapon |
| `grenade_throw` | Grenade thrown | owner, projectile |
| `grenade_explode` | Grenade exploded | owner, projectile |

### Movement Events
| Event | Description | Parameters |
|-------|-------------|------------|
| `player_spawn` | Player spawned | player |
| `player_jump` | Player jumped | player |
| `player_land` | Player landed | player, height |
| `player_crouch` | Player crouched | player |
| `player_prone` | Player went prone | player |
| `player_distance` | Distance traveled | player, walked, sprinted, swam, driven |
| `ladder_mount` | Mounted ladder | player, ladder |
| `ladder_dismount` | Dismounted ladder | player, ladder |

### Interaction Events
| Event | Description | Parameters |
|-------|-------------|------------|
| `player_use` | Player used something | player, entity |
| `player_use_object_start` | Started using object | player, object |
| `player_use_object_finish` | Finished using object | player, object |
| `player_spectate` | Became spectator | player |
| `player_freeze` | Player frozen/unfrozen | player, frozen_state |
| `player_say` | Chat message | player, message |

### Item Events
| Event | Description | Parameters |
|-------|-------------|------------|
| `item_pickup` | Item picked up | player, item_name, amount |
| `item_drop` | Item dropped | player, item_name |
| `health_pickup` | Health picked up | player, amount |
| `ammo_pickup` | Ammo picked up | player, item_name, amount |
| `armor_pickup` | Armor picked up | (via item_pickup) |

### Vehicle/Turret Events
| Event | Description | Parameters |
|-------|-------------|------------|
| `vehicle_enter` | Entered vehicle | player, vehicle |
| `vehicle_exit` | Exited vehicle | player, vehicle |
| `vehicle_death` | Vehicle destroyed | vehicle, attacker |
| `vehicle_collision` | Vehicle collision | vehicle, other |
| `turret_enter` | Entered turret | player, turret |
| `turret_exit` | Exited turret | player, turret |

### Game Flow Events
| Event | Description | Parameters |
|-------|-------------|------------|
| `game_init` | Game initialized | gametype |
| `game_start` | Game started | (none) |
| `game_end` | Game ended | (none) |
| `round_start` | Round started | (none) |
| `round_end` | Round ended | (none) |
| `team_win` | Team won | teamnum |
| `team_join` | Player changed team | player, old_team, new_team |
| `objective_update` | Objective changed | index, status |

### World Events
| Event | Description | Parameters |
|-------|-------------|------------|
| `door_open` | Door opened | door, activator |
| `door_close` | Door closed | door |

### Client Events
| Event | Description | Parameters |
|-------|-------------|------------|
| `client_connect` | Client connected | clientNum |
| `client_begin` | Client began | player |
| `client_userinfo_changed` | Userinfo changed | player |
| `client_disconnect` | Client disconnected | player |

### Bot Events
| Event | Description | Parameters |
|-------|-------------|------------|
| `bot_spawn` | Bot spawned | bot |
| `bot_killed` | Bot killed | bot, attacker |
| `bot_roam` | Bot roaming | bot |
| `bot_curious` | Bot curious | bot |
| `bot_attack` | Bot attacking | bot |

---

## Actor Events

AI Actor control events (255 total).

### AI Control
| Event | Script Command |
|-------|----------------|
| `EV_Actor_AIOn` | `ai_on` |
| `EV_Actor_AIOff` | `ai_off` |
| `EV_Actor_Dumb` | `dumb` |
| `EV_Actor_Physics_On` | `physics_on` |
| `EV_Actor_Physics_Off` | `physics_off` |

### Movement
| Event | Script Command |
|-------|----------------|
| `EV_Actor_MoveTo` | `moveto` |
| `EV_Actor_WalkTo` | `walkto` |
| `EV_Actor_RunTo` | `runto` |
| `EV_Actor_CrawlTo` | `crawlto` |
| `EV_Actor_CrouchTo` | `crouchto` |
| `EV_Actor_Follow` | `follow` |

### Combat
| Event | Script Command |
|-------|----------------|
| `EV_Actor_AttackPlayer` | `attackplayer` |
| `EV_Actor_FindEnemy` | `findenemy` |
| `EV_Actor_EnableEnemy` | `enableenemy` |
| `EV_Actor_AimAt` | `aimat` |
| `EV_Actor_FireGrenade` | `firegrenade` |
| `EV_Actor_AttachGrenade` | `attachgrenade` |
| `EV_Actor_DetachGrenade` | `detachgrenade` |
| `EV_Actor_CalcGrenadeToss` | `calcgrenadetoss` |
| `EV_Actor_CanShoot` | `canshoot` |
| `EV_Actor_ReadyToFire` | `readytofire` |

### Animation
| Event | Script Command |
|-------|----------------|
| `EV_Actor_Anim` | `anim` |
| `EV_Actor_AnimLoop` | `animloop` |
| `EV_Actor_AnimScript` | `animscript` |
| `EV_Actor_SetAnim` | `setanim` |
| `EV_Actor_SayAnim` | `sayanim` |
| `EV_Actor_IdleSayAnim` | `idlesayanim` |
| `EV_Actor_UpperAnim` | `upperanim` |

### Perception
| Event | Script Command |
|-------|----------------|
| `EV_Actor_SetSight` | `sight` |
| `EV_Actor_SetHearing` | `hearing` |
| `EV_Actor_SetFov` | `fov` |
| `EV_Actor_IsEnemyVisible` | `isenemyvisible` |
| `EV_Actor_GetEnemy` | `enemy` |
| `EV_Actor_DistToEnemy` | `distoenemy` |

### Properties (Get/Set)
| Event | Script Command |
|-------|----------------|
| `EV_Actor_SetAccuracy` | `accuracy` |
| `EV_Actor_SetLeash` | `leash` |
| `EV_Actor_SetMood` | `mood` |
| `EV_Actor_SetPatrolPath` | `patrolpath` |
| `EV_Actor_SetTurret` | `turret` |
| `EV_Actor_SetWeapon` | `weapon` |
| `EV_Actor_SetVoiceType` | `voicetype` |
| `EV_Actor_SetNationality` | `nationality` |
| `EV_Actor_SetEmotion` | `emotion` |
| `EV_Actor_SetPosition` | `position` |

### Handlers
| Event | Script Command |
|-------|----------------|
| `EV_Actor_SetAttackHandler` | `attackhandler` |
| `EV_Actor_SetDeathHandler` | `deathhandler` |
| `EV_Actor_SetPainHandler` | `painhandler` |
| `EV_Actor_SetAlarmThread` | `alarmthread` |

### State
| Event | Script Command |
|-------|----------------|
| `EV_Actor_CuriousOn` | `curiouson` |
| `EV_Actor_CuriousOff` | `curiousoff` |
| `EV_Actor_BeDead` | `bedead` |
| `EV_Actor_DeathEmbalm` | `deathembalm` |
| `EV_Actor_Holster` | `holster` |
| `EV_Actor_Unholster` | `unholster` |

---

## ScriptThread Events

Script execution and utility events (234 total).

### Flow Control
| Event | Script Command | Description |
|-------|----------------|-------------|
| `EV_ScriptThread_End` | `end` | End script execution |
| `EV_ScriptThread_Wait` | `wait` | Wait for time |
| `EV_ScriptThread_WaitFrame` | `waitframe` | Wait one frame |
| `EV_ScriptThread_Pause` | `pause` | Pause execution |
| `EV_ScriptThread_Goto` | `goto` | Jump to label |
| `EV_ScriptThread_Timeout` | `timeout` | Set timeout |

### Entity Spawning
| Event | Script Command | Description |
|-------|----------------|-------------|
| `EV_ScriptThread_Spawn` | `spawn` | Spawn entity |
| `EV_ScriptThread_SpawnReturn` | `spawn` | Spawn and return entity |
| `EV_ScriptThread_GetEntity` | `getentity` | Get entity by targetname |
| `EV_ScriptThread_GetEntArray` | `getentarray` | Get entity array |
| `EV_ScriptThread_GetEntByEntnum` | `getentbyentnum` | Get by entity number |

### Math Functions
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_GetRandomFloat` | `randomfloat` |
| `EV_ScriptThread_GetRandomInt` | `randomint` |
| `EV_ScriptThread_GetSin` | `sin` |
| `EV_ScriptThread_GetCos` | `cos` |
| `EV_ScriptThread_GetTan` | `tan` |
| `EV_ScriptThread_GetASin` | `asin` |
| `EV_ScriptThread_GetACos` | `acos` |
| `EV_ScriptThread_GetATan` | `atan` |
| `EV_ScriptThread_GetATan2` | `atan2` |
| `EV_ScriptThread_GetSqrt` | `sqrt` |
| `EV_ScriptThread_GetAbs` | `abs` |
| `EV_ScriptThread_GetFloor` | `floor` |
| `EV_ScriptThread_GetCeil` | `ceil` |
| `EV_ScriptThread_GetExp` | `exp` |
| `EV_ScriptThread_GetLog` | `log` |
| `EV_ScriptThread_GetLog10` | `log10` |
| `EV_ScriptThread_GetPow` | `pow` |

### Vector Operations
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_Vector_Add` | `vector_add` |
| `EV_ScriptThread_Vector_Subtract` | `vector_subtract` |
| `EV_ScriptThread_Vector_Scale` | `vector_scale` |
| `EV_ScriptThread_Vector_Normalize` | `vector_normalize` |
| `EV_ScriptThread_Vector_Length` | `vector_length` |
| `EV_ScriptThread_Vector_DotProduct` | `vector_dot` |
| `EV_ScriptThread_Vector_CrossProduct` | `vector_cross` |
| `EV_ScriptThread_Vector_ToAngles` | `vector_toangles` |
| `EV_ScriptThread_Angles_ToForward` | `angles_toforward` |
| `EV_ScriptThread_Angles_ToLeft` | `angles_toleft` |
| `EV_ScriptThread_Angles_ToUp` | `angles_toup` |

### Type Casting
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_CastInt` | `int` |
| `EV_ScriptThread_CastFloat` | `float` |
| `EV_ScriptThread_CastString` | `string` |
| `EV_ScriptThread_CastBoolean` | `boolean` |
| `EV_ScriptThread_CastEntity` | `entity` |
| `EV_ScriptThread_TypeOf` | `typeof` |
| `EV_ScriptThread_IsDefined` | `isdefined` |
| `EV_ScriptThread_IsArray` | `isarray` |

### Printing/Debug
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_Print` | `print` |
| `EV_ScriptThread_Println` | `println` |
| `EV_ScriptThread_DPrintln` | `dprintln` |
| `EV_ScriptThread_Conprintf` | `conprintf` |
| `EV_ScriptThread_Error` | `error` |
| `EV_ScriptThread_Assert` | `assert` |
| `EV_ScriptThread_DebugLine` | `debugline` |
| `EV_ScriptThread_Print3D` | `print3d` |

### HUD Drawing
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_CreateHUD` | `createhud` |
| `EV_ScriptThread_HudDraw_Shader` | `huddraw_shader` |
| `EV_ScriptThread_HudDraw_String` | `huddraw_string` |
| `EV_ScriptThread_HudDraw_Rect` | `huddraw_rect` |
| `EV_ScriptThread_HudDraw_Color` | `huddraw_color` |
| `EV_ScriptThread_HudDraw_Alpha` | `huddraw_alpha` |
| `EV_ScriptThread_HudDraw_Font` | `huddraw_font` |
| `EV_ScriptThread_HudDraw_Align` | `huddraw_align` |
| `EV_ScriptThread_HudDraw_Timer` | `huddraw_timer` |
| `EV_ScriptThread_HudDraw_3d` | `huddraw_3d` |
| `EV_ScriptThread_HudDraw_VirtualSize` | `huddraw_virtualsize` |

### Screen Effects
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_FadeIn` | `fadein` |
| `EV_ScriptThread_FadeOut` | `fadeout` |
| `EV_ScriptThread_ClearFade` | `clearfade` |
| `EV_ScriptThread_Letterbox` | `letterbox` |
| `EV_ScriptThread_ClearLetterbox` | `clearletterbox` |
| `EV_ScriptThread_VisionSetNaked` | `visionset` |
| `EV_ScriptThread_VisionGetNaked` | `visionget` |

### File Operations
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_FileOpen` | `fopen` |
| `EV_ScriptThread_FileClose` | `fclose` |
| `EV_ScriptThread_FileRead` | `fread` |
| `EV_ScriptThread_FileWrite` | `fwrite` |
| `EV_ScriptThread_FileGets` | `fgets` |
| `EV_ScriptThread_FilePuts` | `fputs` |
| `EV_ScriptThread_FileExists` | `fexists` |
| `EV_ScriptThread_FileList` | `filelist` |
| `EV_ScriptThread_FileRemove` | `fremove` |
| `EV_ScriptThread_FileCopy` | `fcopy` |
| `EV_ScriptThread_FileRename` | `frename` |

### Music/Sound
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_MusicEvent` | `music` |
| `EV_ScriptThread_MusicVolumeEvent` | `musicvolume` |
| `EV_ScriptThread_SoundtrackEvent` | `soundtrack` |
| `EV_ScriptThread_FadeSound` | `fadesound` |
| `EV_ScriptThread_RestoreSound` | `restoresound` |

### Game Control
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_Map` | `map` |
| `EV_ScriptThread_LevelTransition` | `leveltransition` |
| `EV_ScriptThread_MissionTransition` | `missiontransition` |
| `EV_ScriptThread_MissionFailed` | `missionfailed` |
| `EV_ScriptThread_SetCinematic` | `cinematic` |
| `EV_ScriptThread_SetNonCinematic` | `noncinematic` |

### Multiplayer
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_TeamWin` | `teamwin` |
| `EV_ScriptThread_TeamGetScore` | `teamgetscore` |
| `EV_ScriptThread_TeamSetScore` | `teamsetscore` |
| `EV_ScriptThread_CanSwitchTeams` | `canswitchteams` |
| `EV_ScriptThread_TeamSwitchDelay` | `teamswitchdelay` |

### Event Subscription
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_Event_Subscribe` | `event_subscribe` |
| `EV_ScriptThread_Event_Unsubscribe` | `event_unsubscribe` |
| `EV_ScriptThread_RegisterEv` | `registerev` |
| `EV_ScriptThread_UnregisterEv` | `unregisterev` |

### Flags
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_FlagInit` | `flag_init` |
| `EV_ScriptThread_FlagSet` | `flag_set` |
| `EV_ScriptThread_FlagClear` | `flag_clear` |
| `EV_ScriptThread_FlagWait` | `flag_wait` |

### Objectives
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_AddObjective` | `addobjective` |
| `EV_ScriptThread_SetCurrentObjective` | `setcurrentobjective` |
| `EV_ScriptThread_SetObjectiveLocation` | `setobjectivelocation` |
| `EV_ScriptThread_ClearObjectiveLocation` | `clearobjectivelocation` |

### Cvar
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_GetCvar` | `getcvar` |
| `EV_ScriptThread_SetCvar` | `setcvar` |

### Network (HTTP)
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_CurlGet` | `curl_get` |
| `EV_ScriptThread_CurlPost` | `curl_post` |

### Utility
| Event | Script Command |
|-------|----------------|
| `EV_ScriptThread_Trigger` | `trigger` |
| `EV_ScriptThread_Trace` | `trace` |
| `EV_ScriptThread_SightTrace` | `sighttrace` |
| `EV_ScriptThread_TraceDetails` | `tracedetails` |
| `EV_ScriptThread_GetTime` | `gettime` |
| `EV_ScriptThread_GetDate` | `getdate` |
| `EV_ScriptThread_Md5String` | `md5string` |
| `EV_ScriptThread_IsAlive` | `isalive` |
| `EV_ScriptThread_IsOnGround` | `isonground` |
| `EV_ScriptThread_IsOutOfBounds` | `isoutofbounds` |
| `EV_ScriptThread_IsBot` | `isbot` |
| `EV_ScriptThread_PointsWithinDist` | `pointswithindist` |

---

## Player Events

Player-specific events (172 total).

### State
| Event | Script Command |
|-------|----------------|
| `EV_Player_Respawn` | `respawn` |
| `EV_Player_Spectator` | `spectator` |
| `EV_Player_Dead` | `dead` |
| `EV_Player_DeadBody` | `deadbody` |
| `EV_Player_FreezeControls` | `freezecontrols` |
| `EV_Player_EndLevel` | `endlevel` |

### Movement
| Event | Script Command |
|-------|----------------|
| `EV_Player_Jump` | `jump` |
| `EV_Player_Dive` | `dive` |
| `EV_Player_Teleport` | `teleport` |
| `EV_Player_MoveSpeedScale` | `movespeedscale` |
| `EV_Player_SetSpeed` | `setspeed` |
| `EV_Player_ModifyHeight` | `modifyheight` |

### Weapons
| Event | Script Command |
|-------|----------------|
| `EV_Player_DropWeapon` | `dropweapon` |
| `EV_Player_NextWeapon` | `nextweapon` |
| `EV_Player_PrevWeapon` | `prevweapon` |
| `EV_Player_PickWeapon` | `pickweapon` |
| `EV_Player_Reload` | `reload` |
| `EV_Player_Holster` | `holster` |
| `EV_Player_SafeHolster` | `safeholster` |
| `EV_Player_BindWeap` | `bindweap` |

### Teams/DM
| Event | Script Command |
|-------|----------------|
| `EV_Player_JoinDMTeam` | `join_team` |
| `EV_Player_AutoJoinDMTeam` | `auto_join_team` |
| `EV_Player_LeaveTeam` | `leaveteam` |
| `EV_Player_SetTeam` | `setteam` |
| `EV_Player_GetDMTeam` | `dmteam` |
| `EV_Player_PrimaryDMWeapon` | `primarydmweapon` |

### Stats
| Event | Script Command |
|-------|----------------|
| `EV_Player_AddKills` | `addkills` |
| `EV_Player_AddDeaths` | `adddeaths` |
| `EV_Player_GetKills` | `kills` |
| `EV_Player_GetDeaths` | `deaths` |
| `EV_Player_Score` | `score` |
| `EV_Player_Stats` | `stats` |
| `EV_Player_LogStats` | `logstats` |

### View/Camera
| Event | Script Command |
|-------|----------------|
| `EV_Player_Fov` | `fov` |
| `EV_Player_SetFov` | `setfov` |
| `EV_Player_ZoomOff` | `zoomoff` |
| `EV_Player_SafeZoom` | `safezoom` |
| `EV_Player_VisionSetBlur` | `setblur` |
| `EV_Player_VisionSetNaked` | `setvision` |
| `EV_Player_VisionGetNaked` | `getvision` |

### Animation
| Event | Script Command |
|-------|----------------|
| `EV_Player_ForceLegsState` | `forcelegsstate` |
| `EV_Player_ForceTorsoState` | `forcetorsostate` |
| `EV_Player_GetLegsState` | `legsstate` |
| `EV_Player_GetTorsoState` | `torsostate` |
| `EV_Player_AnimLoop_Legs` | `animloop_legs` |
| `EV_Player_AnimLoop_Torso` | `animloop_torso` |
| `EV_Player_SetViewModelAnim` | `viewmodelanim` |

### Properties
| Event | Script Command |
|-------|----------------|
| `EV_Player_GetName` | `netname` |
| `EV_Player_GetUserInfo` | `userinfo` |
| `EV_Player_SetDamageMultiplier` | `damagemult` |
| `EV_Player_GetDamageMultiplier` | `getdamagemult` |
| `EV_Player_SetKillHandler` | `killhandler` |
| `EV_Player_GetKillHandler` | `getkillhandler` |
| `EV_Player_SetStateFile` | `statefile` |
| `EV_Player_GetStateFile` | `getstatefile` |

### Vehicle/Turret
| Event | Script Command |
|-------|----------------|
| `EV_Player_GetVehicle` | `vehicle` |
| `EV_Player_GetTurret` | `turret` |
| `EV_Player_AttachToLadder` | `attachtoladder` |
| `EV_Player_UnattachFromLadder` | `unattachfromladder` |

### Cheats (Dev)
| Event | Script Command |
|-------|----------------|
| `EV_Player_DevGodCheat` | `god` |
| `EV_Player_DevNoClipCheat` | `noclip` |
| `EV_Player_DevNoTargetCheat` | `notarget` |
| `EV_Player_GiveCheat` | `give` |
| `EV_Player_GiveAllCheat` | `giveall` |

### Communication
| Event | Script Command |
|-------|----------------|
| `EV_Player_StuffText` | `stufftext` |
| `EV_Player_IPrint` | `iprint` |
| `EV_Player_DMMessage` | `dmmessage` |
| `EV_Player_PlayLocalSound` | `playlocalsound` |
| `EV_Player_StopLocalSound` | `stoplocalsound` |

### Jail (2.30+)
| Event | Script Command |
|-------|----------------|
| `EV_Player_GetInJail` | `injail` |
| `EV_Player_SetInJail` | `setinjail` |
| `EV_Player_JailEscape` | `jailescape` |
| `EV_Player_JailEscapeStop` | `jailescapestop` |
| `EV_Player_JailIsEscaping` | `isescaping` |
| `EV_Player_JailAssistEscape` | `jailassistescape` |

### Use Objects
| Event | Script Command |
|-------|----------------|
| `EV_Player_DoUse` | `douse` |
| `EV_Player_StartUseObject` | `startuseobject` |
| `EV_Player_FinishUseObject` | `finishuseobject` |

---

## Weapon Events

Weapon configuration and behavior (89 total).

### Basic Properties
| Event | Script Command |
|-------|----------------|
| `EV_Weapon_AmmoType` | `ammotype` |
| `EV_Weapon_StartAmmo` | `startammo` |
| `EV_Weapon_SetAmmoClipSize` | `clipsize` |
| `EV_Weapon_SetAmmoInClip` | `ammo_in_clip` |
| `EV_Weapon_FireDelay` | `firedelay` |
| `EV_Weapon_SetRange` | `range` |
| `EV_Weapon_SetType` | `weapontype` |

### Firing
| Event | Script Command |
|-------|----------------|
| `EV_Weapon_Shoot` | `shoot` |
| `EV_Weapon_DoneFiring` | `donefiring` |
| `EV_Weapon_SetFireType` | `firetype` |
| `EV_Weapon_SetLoopFire` | `loopfire` |
| `EV_Weapon_SetSemiAuto` | `semiauto` |

### Bullets
| Event | Script Command |
|-------|----------------|
| `EV_Weapon_SetBulletDamage` | `bulletdamage` |
| `EV_Weapon_SetBulletCount` | `bulletcount` |
| `EV_Weapon_SetBulletSpread` | `bulletspread` |
| `EV_Weapon_SetBulletKnockback` | `bulletknockback` |
| `EV_Weapon_SetBulletLarge` | `bulletlarge` |
| `EV_Weapon_SetBulletThroughWood` | `throughwood` |
| `EV_Weapon_SetBulletThroughMetal` | `throughmetal` |

### Projectiles
| Event | Script Command |
|-------|----------------|
| `EV_Weapon_SetProjectile` | `projectile` |
| `EV_Weapon_SetDMProjectile` | `dmprojectile` |

### View
| Event | Script Command |
|-------|----------------|
| `EV_Weapon_Crosshair` | `crosshair` |
| `EV_Weapon_AutoAim` | `autoaim` |
| `EV_Weapon_SetZoom` | `zoom` |
| `EV_Weapon_SetViewKick` | `viewkick` |
| `EV_Weapon_SetAimAnim` | `aimanim` |

### Holster/Tags
| Event | Script Command |
|-------|----------------|
| `EV_Weapon_HolsterTag` | `holstertag` |
| `EV_Weapon_HolsterOffset` | `holsteroffset` |
| `EV_Weapon_HolsterAngles` | `holsterangles` |
| `EV_Weapon_HolsterScale` | `holsterscale` |
| `EV_Weapon_MainAttachToTag` | `mainattachtotag` |
| `EV_Weapon_OffHandAttachToTag` | `offhandattachtotag` |

### Reload
| Event | Script Command |
|-------|----------------|
| `EV_Weapon_DoneReloading` | `donereloading` |
| `EV_Weapon_FillClip` | `fillclip` |
| `EV_Weapon_EmptyClip` | `emptyclip` |
| `EV_Weapon_AddToClip` | `addtoclip` |
| `EV_Weapon_CantPartialReload` | `cantpartialreload` |

### Animation
| Event | Script Command |
|-------|----------------|
| `EV_Weapon_Idle` | `idle` |
| `EV_Weapon_DoneRaising` | `doneraising` |
| `EV_Weapon_DoneAnimating` | `doneanimating` |
| `EV_Weapon_NumFireAnims` | `numfireanims` |
| `EV_Weapon_SetCurrentFireAnim` | `currentfireanim` |

### DM Overrides
| Event | Script Command |
|-------|----------------|
| `EV_Weapon_DMSetFireDelay` | `dmfiredelay` |
| `EV_Weapon_SetDMBulletDamage` | `dmbulletdamage` |
| `EV_Weapon_SetDMBulletCount` | `dmbulletcount` |
| `EV_Weapon_SetDMBulletSpread` | `dmbulletspread` |
| `EV_Weapon_SetDMBulletRange` | `dmbulletrange` |
| `EV_Weapon_DMMovementSpeed` | `dmmovementspeed` |
| `EV_Weapon_DMCrosshair` | `dmcrosshair` |

### Miscellaneous
| Event | Script Command |
|-------|----------------|
| `EV_Weapon_NotDroppable` | `notdroppable` |
| `EV_Weapon_SetQuiet` | `quiet` |
| `EV_Weapon_MakeNoise` | `makenoise` |
| `EV_Weapon_SetAIRange` | `airange` |
| `EV_Weapon_SetMeansOfDeath` | `meansofdeath` |
| `EV_Weapon_SetTracerFrequency` | `tracerfrequency` |
| `EV_Weapon_SetTracerSpeed` | `tracerspeed` |
| `EV_Weapon_SetGroup` | `weapongroup` |
| `EV_Weapon_Secondary` | `secondary` |

---

## Vehicle Events

Vehicle control and configuration (87 total).

### State
| Event | Script Command |
|-------|----------------|
| `EV_Vehicle_Start` | `start` |
| `EV_Vehicle_Stop` | `stop` |
| `EV_Vehicle_FullStop` | `fullstop` |
| `EV_Vehicle_Lock` | `lock` |
| `EV_Vehicle_UnLock` | `unlock` |
| `EV_Vehicle_Drivable` | `drivable` |
| `EV_Vehicle_UnDrivable` | `undrivable` |
| `EV_Vehicle_Destroyed` | `destroyed` |

### Entry/Exit
| Event | Script Command |
|-------|----------------|
| `EV_Vehicle_Enter` | `enter` |
| `EV_Vehicle_Exit` | `exit` |
| `EV_Vehicle_CanUse` | `canuse` |
| `EV_Vehicle_Jumpable` | `jumpable` |

### Slots
| Event | Script Command |
|-------|----------------|
| `EV_Vehicle_AttachDriverSlot` | `attachdriverslot` |
| `EV_Vehicle_DetachDriverSlot` | `detachdriverslot` |
| `EV_Vehicle_AttachPassengerSlot` | `attachpassengerslot` |
| `EV_Vehicle_DetachPassengerSlot` | `detachpassengerslot` |
| `EV_Vehicle_AttachTurretSlot` | `attachturretslot` |
| `EV_Vehicle_DetachTurretSlot` | `detachturretslot` |
| `EV_Vehicle_QueryFreeDriverSlot` | `queryfreedriverslot` |
| `EV_Vehicle_QueryFreePassengerSlot` | `queryfreepassengerslot` |
| `EV_Vehicle_QueryFreeTurretSlot` | `queryfreeturretslot` |

### Movement
| Event | Script Command |
|-------|----------------|
| `EV_Vehicle_Drive` | `drive` |
| `EV_Vehicle_DriveNoWait` | `drivenowait` |
| `EV_Vehicle_NextDrive` | `nextdrive` |
| `EV_Vehicle_ModifyDrive` | `modifydrive` |
| `EV_Vehicle_SetSpeed` | `speed` |
| `EV_Vehicle_SetTurnRate` | `turnrate` |
| `EV_Vehicle_SteerInPlace` | `steerinplace` |
| `EV_Vehicle_StopAtEnd` | `stopatend` |

### Physics
| Event | Script Command |
|-------|----------------|
| `EV_Vehicle_Mass` | `mass` |
| `EV_Vehicle_Front_Mass` | `front_mass` |
| `EV_Vehicle_Back_Mass` | `back_mass` |
| `EV_Vehicle_Drag` | `drag` |
| `EV_Vehicle_RollingResistance` | `rollingresistance` |
| `EV_Vehicle_BouncyCoef` | `bouncycoef` |
| `EV_Vehicle_SpringyCoef` | `springycoef` |

### Sounds
| Event | Script Command |
|-------|----------------|
| `EV_Vehicle_SoundSet` | `vehiclesoundset` |
| `EV_Vehicle_RunSounds` | `runsounds` |
| `EV_Vehicle_DamageSounds` | `damagesounds` |
| `EV_Vehicle_SetSoundParameters` | `soundparameters` |
| `EV_Vehicle_SetVolumeParameters` | `volumeparameters` |

### Animation
| Event | Script Command |
|-------|----------------|
| `EV_Vehicle_VehicleAnim` | `vehicleanim` |
| `EV_Vehicle_VehicleAnimDone` | `vehicleanimdone` |
| `EV_Vehicle_VehicleMoveAnim` | `vehiclemoveanim` |
| `EV_Vehicle_AnimationSet` | `animationset` |
| `EV_Vehicle_Tread` | `tread` |

### Weapons
| Event | Script Command |
|-------|----------------|
| `EV_Vehicle_SetWeapon` | `weaponname` |
| `EV_Vehicle_ShowWeapon` | `showweapon` |
| `EV_Vehicle_SpawnTurret` | `spawnturret` |

---

## Sentient Events

Base events for all living entities (64 total).

### Health/Damage
| Event | Script Command |
|-------|----------------|
| `EV_Heal` | `heal` |
| `EV_Damage` | `damage` |
| `EV_Killed` | `killed` |
| `EV_Pain` | `pain` |
| `EV_Stun` | `stun` |
| `EV_Sentient_StunStart` | `stunstart` |
| `EV_Sentient_StunEnd` | `stunend` |

### Items/Weapons
| Event | Script Command |
|-------|----------------|
| `EV_Sentient_GiveWeapon` | `weapon` |
| `EV_Sentient_GiveAmmo` | `ammo` |
| `EV_Sentient_GiveArmor` | `armor` |
| `EV_Sentient_GiveItem` | `item` |
| `EV_Sentient_Take` | `take` |
| `EV_Sentient_TakeAll` | `takeall` |
| `EV_Sentient_DropItems` | `dropitems` |
| `EV_Sentient_ForceDropWeapon` | `forcedropweapon` |
| `EV_Sentient_DontDropWeapons` | `dontdropweapons` |

### Weapon Control
| Event | Script Command |
|-------|----------------|
| `EV_Sentient_UseItem` | `useitem` |
| `EV_Sentient_UseWeaponClass` | `useweaponclass` |
| `EV_Sentient_UseLastWeapon` | `uselastweapon` |
| `EV_Sentient_ReloadWeapon` | `reloadweapon` |
| `EV_Sentient_PutawayWeapon` | `putawayweapon` |
| `EV_Sentient_ActivateNewWeapon` | `activatenewweapon` |
| `EV_Sentient_DeactivateWeapon` | `deactivateweapon` |
| `EV_Sentient_GetActiveWeap` | `activeweapon` |

### Attack
| Event | Script Command |
|-------|----------------|
| `EV_Sentient_Attack` | `fire` |
| `EV_Sentient_ReleaseAttack` | `releasefire` |
| `EV_Sentient_StopFire` | `stopfire` |
| `EV_Sentient_Charge` | `charge` |
| `EV_Sentient_MeleeAttackStart` | `meleeattackstart` |
| `EV_Sentient_MeleeAttackEnd` | `meleeattackend` |
| `EV_Sentient_BlockStart` | `blockstart` |
| `EV_Sentient_BlockEnd` | `blockend` |

### Team
| Event | Script Command |
|-------|----------------|
| `EV_Sentient_GetTeam` | `team` |
| `EV_Sentient_American` | `american` |
| `EV_Sentient_German` | `german` |

### Properties
| Event | Script Command |
|-------|----------------|
| `EV_Sentient_SetDamageMult` | `damagemult` |
| `EV_Sentient_SetThreatBias` | `threatbias` |
| `EV_Sentient_GetThreatBias` | `getthreatbias` |
| `EV_Sentient_SetBloodModel` | `bloodmodel` |
| `EV_Sentient_SetMaxGibs` | `maxgibs` |

---

## Level Events

Global level/game state events (59 total).

### Time
| Event | Script Command |
|-------|----------------|
| `EV_Level_GetTime` | `time` |

### Game Mode
| Event | Script Command |
|-------|----------------|
| `EV_Level_GetRoundBased` | `roundbased` |
| `EV_Level_GetObjectiveBased` | `objectivebased` |
| `EV_Level_GetDMRespawning` | `dmrespawning` |
| `EV_Level_SetDMRespawning` | `setdmrespawning` |
| `EV_Level_GetDMRoundLimit` | `dmroundlimit` |
| `EV_Level_SetDMRoundLimit` | `setdmroundlimit` |
| `EV_Level_GetRoundStarted` | `roundstarted` |

### Objectives
| Event | Script Command |
|-------|----------------|
| `EV_Level_GetTargetsToDestroy` | `targets_to_destroy` |
| `EV_Level_SetTargetsToDestroy` | `set_targets_to_destroy` |
| `EV_Level_GetTargetsDestroyed` | `targets_destroyed` |
| `EV_Level_SetTargetsDestroyed` | `set_targets_destroyed` |

### Bombs
| Event | Script Command |
|-------|----------------|
| `EV_Level_GetBombsPlanted` | `bombsplanted` |
| `EV_Level_SetBombsPlanted` | `setbombsplanted` |
| `EV_Level_GetBombPlantTeam` | `bombplantteam` |
| `EV_Level_SetBombPlantTeam` | `setbombplantteam` |

### Clock
| Event | Script Command |
|-------|----------------|
| `EV_Level_GetClockSide` | `clockside` |
| `EV_Level_SetClockSide` | `setclockside` |
| `EV_Level_IgnoreClock` | `ignoreclock` |

### Alarm
| Event | Script Command |
|-------|----------------|
| `EV_Level_GetAlarm` | `alarm` |
| `EV_Level_SetAlarm` | `setalarm` |

### Papers (Disguise)
| Event | Script Command |
|-------|----------------|
| `EV_Level_GetPapersLevel` | `paperslevel` |
| `EV_Level_SetPapersLevel` | `setpaperslevel` |

### Rain
| Event | Script Command |
|-------|----------------|
| `EV_Level_Rain_Density_Get` | `rain_density` |
| `EV_Level_Rain_Density_Set` | `set_rain_density` |
| `EV_Level_Rain_Speed_Get` | `rain_speed` |
| `EV_Level_Rain_Speed_Set` | `set_rain_speed` |
| `EV_Level_Rain_Length_Get` | `rain_length` |
| `EV_Level_Rain_Length_Set` | `set_rain_length` |
| `EV_Level_Rain_Width_Get` | `rain_width` |
| `EV_Level_Rain_Width_Set` | `set_rain_width` |
| `EV_Level_Rain_Shader_Get` | `rain_shader` |
| `EV_Level_Rain_Shader_Set` | `set_rain_shader` |
| `EV_Level_Rain_Slant_Get` | `rain_slant` |
| `EV_Level_Rain_Slant_Set` | `set_rain_slant` |

### Bad Places
| Event | Script Command |
|-------|----------------|
| `EV_Level_AddBadPlace` | `addbadplace` |
| `EV_Level_RemoveBadPlace` | `removebadplace` |

---

## World Events

World/environment settings (50 total).

### Far Plane/Fog
| Event | Script Command |
|-------|----------------|
| `EV_World_SetFarPlane` | `farplane` |
| `EV_World_GetFarPlane` | `getfarplane` |
| `EV_World_SetFarPlaneBias` | `farplane_bias` |
| `EV_World_GetFarPlaneBias` | `getfarplane_bias` |
| `EV_World_SetFarPlane_Color` | `farplane_color` |
| `EV_World_GetFarPlane_Color` | `getfarplane_color` |
| `EV_World_SetFarPlane_Cull` | `farplane_cull` |
| `EV_World_SetFarClipOverride` | `farclipoverride` |
| `EV_World_SetFarPlaneColorOverride` | `farplanecoloroverride` |
| `EV_World_SetAnimatedFarPlane` | `animatedfarplane` |
| `EV_World_SetAnimatedFarPlaneBias` | `animatedfarplanebias` |
| `EV_World_SetAnimatedFarPlaneColor` | `animatedfarplanecolor` |
| `EV_World_UpdateAnimatedFarplane` | `updateanimatedfarplane` |

### Skybox
| Event | Script Command |
|-------|----------------|
| `EV_World_SetSkyboxFarPlane` | `skybox_farplane` |
| `EV_World_GetSkyboxFarPlane` | `getskybox_farplane` |
| `EV_World_SetSkyboxSpeed` | `skybox_speed` |
| `EV_World_GetSkyboxSpeed` | `getskybox_speed` |
| `EV_World_SetSkyAlpha` | `skyalpha` |
| `EV_World_SetSkyPortal` | `skyportal` |

### Sun/Light
| Event | Script Command |
|-------|----------------|
| `EV_World_SetSunColor` | `suncolor` |
| `EV_World_SetSunLight` | `sunlight` |
| `EV_World_SetSunDiffuse` | `sundiffuse` |
| `EV_World_SetSunDiffuseColor` | `sundiffusecolor` |
| `EV_World_SetSunDirection` | `sundirection` |
| `EV_World_SunFlareDirection` | `sunflaredirection` |
| `EV_World_SunFlareName` | `sunflarename` |
| `EV_World_SetAmbientLight` | `ambient` |
| `EV_World_SetAmbientIntensity` | `ambientintensity` |

### Water
| Event | Script Command |
|-------|----------------|
| `EV_World_SetWaterColor` | `watercolor` |
| `EV_World_SetWaterAlpha` | `wateralpha` |
| `EV_World_SetLavaColor` | `lavacolor` |
| `EV_World_SetLavaAlpha` | `lavaalpha` |

### Game
| Event | Script Command |
|-------|----------------|
| `EV_World_SetGravity` | `gravity` |
| `EV_World_SetMessage` | `message` |
| `EV_World_SetNextMap` | `nextmap` |
| `EV_World_SetSoundtrack` | `soundtrack` |
| `EV_World_MapTime` | `maptime` |
| `EV_World_SetNorthYaw` | `northyaw` |
| `EV_World_SetAIVisionDistance` | `ai_visiondistance` |
| `EV_World_SetNumArenas` | `numarenas` |

### Rendering
| Event | Script Command |
|-------|----------------|
| `EV_World_SetRenderTerrain` | `renderterrain` |
| `EV_World_GetRenderTerrain` | `getrenderterrain` |
| `EV_World_LightmapDensity` | `lightmapdensity` |
| `EV_World_Overbright` | `overbright` |
| `EV_World_VisDerived` | `visderived` |

---

## Projectile Events

Projectile configuration (40 total).

| Event | Script Command | Description |
|-------|----------------|-------------|
| `EV_Projectile_Speed` | `speed` | Set projectile speed |
| `EV_Projectile_MinSpeed` | `minspeed` | Minimum speed |
| `EV_Projectile_Damage` | `damage` | Damage amount |
| `EV_Projectile_Knockback` | `knockback` | Knockback force |
| `EV_Projectile_Life` | `life` | Projectile lifetime |
| `EV_Projectile_MinLife` | `minlife` | Minimum lifetime |
| `EV_Projectile_DMLife` | `dmlife` | DM lifetime |
| `EV_Projectile_MeansOfDeath` | `meansofdeath` | Damage type |
| `EV_Projectile_Explode` | `explode` | Explode now |
| `EV_Projectile_ExplodeOnTouch` | `explodeontouch` | Touch explosion |
| `EV_Projectile_DieInWater` | `dieinwater` | Die in water |
| `EV_Projectile_ArcToTarget` | `arctotarget` | Arc trajectory |
| `EV_Projectile_HeatSeek` | `heatseek` | Heat seeking |
| `EV_Projectile_Drunk` | `drunk` | Drunk movement |
| `EV_Projectile_Avelocity` | `avelocity` | Angular velocity |
| `EV_Projectile_BounceTouch` | `bouncetouch` | Bounce on touch |
| `EV_Projectile_BounceSound` | `bouncesound` | Bounce sound |
| `EV_Projectile_DLight` | `dlight` | Dynamic light |
| `EV_Projectile_SetFuse` | `fuse` | Fuse timer |
| `EV_Projectile_SetExplosionModel` | `explosionmodel` | Explosion model |
| `EV_Projectile_ImpactMarkShader` | `impactmarkshader` | Impact decal |
| `EV_Projectile_ImpactMarkRadius` | `impactmarkradius` | Decal size |
| `EV_Projectile_SetSmashThroughGlass` | `smashthroughglass` | Break glass |
| `EV_Projectile_BecomeBomb` | `becomebomb` | Become bomb |
| `EV_Projectile_ChargeSpeed` | `chargespeed` | Charged speed |
| `EV_Projectile_ChargeLife` | `chargelife` | Charged life |
| `EV_Projectile_SetCanHitOwner` | `canhitowner` | Can hit owner |
| `EV_Projectile_ClearOwner` | `clearowner` | Clear owner |
| `EV_Projectile_NoTouchDamage` | `notouchdamage` | No touch damage |
| `EV_Projectile_AddOwnerVelocity` | `addownervelocity` | Add owner velocity |

---

## Turret Events

Turret configuration (35 total).

### Player Turrets
| Event | Script Command |
|-------|----------------|
| `EV_Turret_Enter` | `enter` |
| `EV_Turret_Exit` | `exit` |
| `EV_Turret_SetUsable` | `usable` |
| `EV_Turret_SetMaxUseAngle` | `maxuseangle` |
| `EV_Turret_P_SetPlayerUsable` | `playerusable` |
| `EV_Turret_P_SetThread` | `setthread` |
| `EV_Turret_P_SetViewAngles` | `viewangles` |
| `EV_Turret_P_ViewOffset` | `viewoffset` |
| `EV_Turret_P_ViewJitter` | `viewjitter` |
| `EV_Turret_P_UserDistance` | `userdistance` |

### Aiming
| Event | Script Command |
|-------|----------------|
| `EV_Turret_PitchCaps` | `pitchcaps` |
| `EV_Turret_YawCenter` | `yawcenter` |
| `EV_Turret_MaxYawOffset` | `maxyawoffset` |
| `EV_Turret_MaxIdlePitch` | `maxidlepitch` |
| `EV_Turret_MaxIdleYaw` | `maxidleyaw` |
| `EV_Turret_SetStartYaw` | `startyaw` |
| `EV_Turret_IdleCheckOffset` | `idlecheckoffset` |

### AI Control
| Event | Script Command |
|-------|----------------|
| `EV_Turret_AI_SetAimTarget` | `setaimtarget` |
| `EV_Turret_AI_ClearAimTarget` | `clearaimtarget` |
| `EV_Turret_AI_SetAimOffset` | `setaimoffset` |
| `EV_Turret_AI_SetTargetType` | `settargettype` |
| `EV_Turret_AI_GetTargetType` | `gettargettype` |
| `EV_Turret_AI_TurnSpeed` | `turnspeed` |
| `EV_Turret_AI_PitchSpeed` | `pitchspeed` |
| `EV_Turret_AI_ConvergeTime` | `convergetime` |
| `EV_Turret_AI_StartFiring` | `startfiring` |
| `EV_Turret_AI_StopFiring` | `stopfiring` |
| `EV_Turret_AI_BurstFireSettings` | `burstfiresettings` |
| `EV_Turret_AI_SetBulletSpread` | `setbulletspread` |
| `EV_Turret_AI_SuppressTime` | `suppresstime` |
| `EV_Turret_AI_SuppressWidth` | `suppresswidth` |
| `EV_Turret_AI_SuppressHeight` | `suppressheight` |
| `EV_Turret_AI_SuppressWaitTime` | `suppresswaittime` |

---

## Door Events

Door control and configuration (25 total).

| Event | Script Command | Description |
|-------|----------------|-------------|
| `EV_Door_Open` | `open` | Open door |
| `EV_Door_Close` | `close` | Close door |
| `EV_Door_DoOpen` | `doopen` | Force open |
| `EV_Door_DoClose` | `doclose` | Force close |
| `EV_Door_Lock` | `lock` | Lock door |
| `EV_Door_Unlock` | `unlock` | Unlock door |
| `EV_Door_IsOpen` | `isopen` | Check if open |
| `EV_Door_SetTime` | `time` | Open/close time |
| `EV_Door_SetWait` | `wait` | Wait time |
| `EV_Door_SetDmg` | `dmg` | Crush damage |
| `EV_Door_AlwaysAway` | `alwaysaway` | Open away |
| `EV_Door_DoorType` | `doortype` | Door type |
| `EV_Door_OpenStartSound` | `openstartsound` | Sound |
| `EV_Door_OpenEndSound` | `openendsound` | Sound |
| `EV_Door_CloseStartSound` | `closestartsound` | Sound |
| `EV_Door_CloseEndSound` | `closeendsound` | Sound |
| `EV_Door_LockedSound` | `lockedsound` | Sound |
| `EV_Door_MessageSound` | `messagesound` | Sound |
| `EV_RotatingDoor_OpenAngle` | `openangle` | Open angle |
| `EV_SlidingDoor_SetLip` | `lip` | Lip size |
| `EV_SlidingDoor_SetSpeed` | `speed` | Speed |

---

## Trigger Events

Trigger entities (24+ total).

### Base Trigger
| Event | Script Command |
|-------|----------------|
| `EV_Trigger` | (touch) |
| `EV_Trigger_ActivateTargets` | `activatetargets` |
| `EV_Trigger_StartThread` | `startthread` |
| `EV_Trigger_SetThread` | `thread` |
| `EV_Trigger_SetWait` | `wait` |
| `EV_Trigger_SetDelay` | `delay` |
| `EV_Trigger_SetCount` | `count` |
| `EV_Trigger_SetMessage` | `message` |
| `EV_Trigger_SetNoise` | `noise` |
| `EV_Trigger_SetSound` | `sound` |
| `EV_Trigger_SetChance` | `chance` |
| `EV_Trigger_SetTriggerable` | `triggerable` |
| `EV_Trigger_SetNotTriggerable` | `nottriggerable` |
| `EV_Trigger_SetDamageable` | `damageable` |
| `EV_Trigger_SetEdgeTriggered` | `edgetriggered` |
| `EV_Trigger_SetMultiFaceted` | `multifaceted` |
| `EV_Trigger_SetTriggerCone` | `triggercone` |
| `EV_Trigger_GetActivator` | `activator` |
| `EV_Trigger_IsAbandoned` | `isabandoned` |
| `EV_Trigger_IsImmune` | `isimmune` |

### Trigger Types
| Event | Description |
|-------|-------------|
| `EV_TriggerChangeLevel_Map` | Level change target |
| `EV_TriggerChangeLevel_SpawnSpot` | Spawn location |
| `EV_TriggerHurt_SetDamage` | Hurt trigger damage |
| `EV_TriggerHurt_SetDamageType` | Damage type |
| `EV_TriggerPush_SetPushSpeed` | Push speed |
| `EV_TriggerMusic_CurrentMood` | Music mood |
| `EV_TriggerMusic_FallbackMood` | Fallback mood |
| `EV_TriggerReverb_ReverbType` | Reverb type |
| `EV_TriggerReverb_ReverbLevel` | Reverb level |
| `EV_TriggerSave_SaveName` | Save name |
| `EV_TriggerGivePowerup_PowerupName` | Powerup name |
| `EV_TriggerDamageTargets_SetDamage` | Damage amount |
| `EV_TriggerPlaySound_SetVolume` | Sound volume |
| `EV_TriggerPlaySound_SetChannel` | Sound channel |
| `EV_TriggerBox_SetMins` | Trigger bounds |
| `EV_TriggerBox_SetMaxs` | Trigger bounds |

---

## Item Events

Item/pickup events (18 total).

| Event | Script Command | Description |
|-------|----------------|-------------|
| `EV_Item_Pickup` | `pickup` | Item pickup trigger |
| `EV_Item_PickupDone` | `pickupdone` | Pickup complete |
| `EV_Item_DropToFloor` | `droptofloor` | Drop to ground |
| `EV_Item_Respawn` | `respawn` | Respawn item |
| `EV_Item_RespawnDone` | `respawndone` | Respawn complete |
| `EV_Item_SetAmount` | `amount` | Set amount |
| `EV_Item_SetMaxAmount` | `maxamount` | Set max amount |
| `EV_Item_SetDMAmount` | `dmamount` | DM amount |
| `EV_Item_SetDMMaxAmount` | `dmmaxamount` | DM max amount |
| `EV_Item_SetRespawn` | `respawn` | Enable respawn |
| `EV_Item_SetRespawnTime` | `respawntime` | Respawn time |
| `EV_Item_SetPickupSound` | `pickupsound` | Pickup sound |
| `EV_Item_RespawnSound` | `respawnsound` | Respawn sound |
| `EV_Item_SetItemName` | `name` | Item name |
| `EV_Item_NoRemove` | `noremove` | Don't remove |
| `EV_Item_DialogNeeded` | `dialogneeded` | Need dialog |
| `EV_Item_ViewModelPrefix` | `viewmodelprefix` | ViewModel prefix |
| `EV_Item_UpdatePrefix` | `updateprefix` | Update prefix |

---

## HUD Events

HUD element control (29 total).

| Event | Script Command | Description |
|-------|----------------|-------------|
| `EV_HUD_SetShader` | `shader` | Set shader |
| `EV_HUD_SetText` | `text` | Set text |
| `EV_HUD_SetFont` | `font` | Set font |
| `EV_HUD_SetRectX` | `rectx` | X position |
| `EV_HUD_SetRectY` | `recty` | Y position |
| `EV_HUD_GetRectX` | `getrectx` | Get X |
| `EV_HUD_GetRectY` | `getrecty` | Get Y |
| `EV_HUD_GetWidth` | `getwidth` | Get width |
| `EV_HUD_GetHeight` | `getheight` | Get height |
| `EV_HUD_SetColor` | `color` | Set color |
| `EV_HUD_GetColor` | `getcolor` | Get color |
| `EV_HUD_SetAlpha` | `alpha` | Set alpha |
| `EV_HUD_GetAlpha` | `getalpha` | Get alpha |
| `EV_HUD_SetAlignX` | `alignx` | X alignment |
| `EV_HUD_SetAlignY` | `aligny` | Y alignment |
| `EV_HUD_GetAlignX` | `getalignx` | Get X align |
| `EV_HUD_GetAlignY` | `getaligny` | Get Y align |
| `EV_HUD_SetTimer` | `timer` | Set countdown |
| `EV_HUD_SetTimerUp` | `timerup` | Set countup |
| `EV_HUD_GetTime` | `gettime` | Get timer |
| `EV_HUD_SetVirtualSize` | `virtualsize` | Virtual sizing |
| `EV_HUD_Set3D` | `3d` | 3D mode |
| `EV_HUD_SetNon3D` | `non3d` | 2D mode |
| `EV_HUD_SetPlayer` | `player` | Set player |
| `EV_HUD_MoveOverTime` | `moveovertime` | Animate move |
| `EV_HUD_ScaleOverTime` | `scaleovertime` | Animate scale |
| `EV_HUD_FadeOverTime` | `fadeovertime` | Animate fade |
| `EV_HUD_Refresh` | `refresh` | Refresh HUD |

---

## Base Entity Events

Common to all entities (30+ total).

### Transform
| Event | Script Command |
|-------|----------------|
| `EV_SetOrigin` | `origin` |
| `EV_GetOrigin` | `getorigin` |
| `EV_SetAngles` | `angles` |
| `EV_GetAngles` | `getangles` |
| `EV_SetScale` | `scale` |
| `EV_GetScale` | `getscale` |
| `EV_SetVelocity` | `velocity` |
| `EV_GetVelocity` | `getvelocity` |

### Model/Visual
| Event | Script Command |
|-------|----------------|
| `EV_SetModel` | `model` |
| `EV_GetModel` | `getmodel` |
| `EV_Show` | `show` |
| `EV_Hide` | `hide` |
| `EV_Ghost` | `ghost` |
| `EV_SetAlpha` | `alpha` |
| `EV_AlwaysDraw` | `alwaysdraw` |
| `EV_NeverDraw` | `neverdraw` |
| `EV_NormalDraw` | `normaldraw` |

### Collision
| Event | Script Command |
|-------|----------------|
| `EV_BecomeSolid` | `solid` |
| `EV_BecomeNonSolid` | `notsolid` |
| `EV_SetSize` | `setsize` |
| `EV_SetMins` | `mins` |
| `EV_SetMaxs` | `maxs` |
| `EV_UseBoundingBox` | `useboundingbox` |

### Targeting
| Event | Script Command |
|-------|----------------|
| `EV_SetTarget` | `target` |
| `EV_GetTarget` | `gettarget` |
| `EV_SetTargetname` | `targetname` |
| `EV_GetTargetname` | `gettargetname` |

### Health
| Event | Script Command |
|-------|----------------|
| `EV_SetHealth` | `health` |
| `EV_Entity_GetHealth` | `gethealth` |
| `EV_Entity_SetMaxHealth` | `maxhealth` |
| `EV_Entity_GetMaxHealth` | `getmaxhealth` |

### Parenting
| Event | Script Command |
|-------|----------------|
| `EV_Bind` | `bind` |
| `EV_Unbind` | `unbind` |
| `EV_Glue` | `glue` |
| `EV_Unglue` | `unglue` |
| `EV_Attach` | `attach` |
| `EV_Detach` | `detach` |
| `EV_AttachModel` | `attachmodel` |
| `EV_RemoveAttachedModel` | `removeattachedmodel` |
| `EV_DetachAllChildren` | `detachallchildren` |

### Actions
| Event | Script Command |
|-------|----------------|
| `EV_Use` | `use` |
| `EV_Trigger` | `trigger` |
| `EV_Activate` | `activate` |
| `EV_Kill` | `kill` |
| `EV_Remove` | `remove` |
| `EV_TakeDamage` | `takedamage` |
| `EV_NoDamage` | `nodamage` |
| `EV_NormalDamage` | `normaldamage` |

### Sound
| Event | Script Command |
|-------|----------------|
| `EV_Sound` | `playsound` |
| `EV_StopSound` | `stopsound` |
| `EV_LoopSound` | `loopsound` |
| `EV_StopLoopSound` | `stoploopsound` |

### Vectors
| Event | Script Command |
|-------|----------------|
| `EV_ForwardVector` | `forwardvector` |
| `EV_RightVector` | `rightvector` |
| `EV_UpVector` | `upvector` |
| `EV_LeftVector` | `leftvector` |

### Tags
| Event | Script Command |
|-------|----------------|
| `EV_GetTagPosition` | `gettagposition` |
| `EV_GetTagAngles` | `gettagangles` |

### Queries
| Event | Script Command |
|-------|----------------|
| `EV_GetEntnum` | `entnum` |
| `EV_GetClassname` | `classname` |
| `EV_IsTouching` | `istouching` |
| `EV_IsInside` | `isinside` |
| `EV_CanSee` | `cansee` |
| `EV_CanSeeNoEnts` | `canseenoents` |
| `EV_Entity_InPVS` | `inpvs` |

---

## Summary Statistics

| Category | Count |
|----------|-------|
| ScriptDelegate Events | 67 |
| Actor Events | 255 |
| ScriptThread Events | 234 |
| Player Events | 172 |
| Weapon Events | 89 |
| Vehicle Events | 87 |
| Sentient Events | 64 |
| ScriptSlave Events | 63 |
| Level Events | 59 |
| World Events | 50 |
| Projectile Events | 40 |
| Turret Events | 35 |
| Camera Events | 34 |
| Entity Events | 30+ |
| HUD Events | 29 |
| ViewThing Events | 27 |
| CameraManager Events | 27 |
| FuncBeam Events | 25 |
| Door Events | 25 |
| Trigger Events | 24+ |
| UseObject Events | 19 |
| SoundManager Events | 19 |
| Item Events | 18 |
| VehicleTurretGun Events | 16 |
| Miscellaneous | 200+ |
| **TOTAL** | **~2,009** |

---

## Usage Examples

### Subscribing to Events
```morpheus
main:
    // Subscribe to player kill events
    event_subscribe "player_kill" "on_player_kill"
    event_subscribe "weapon_fire" "on_weapon_fire"
    event_subscribe "weapon_hit" "on_weapon_hit"
end

on_player_kill:
    local.killer = parm.get 1
    local.victim = parm.get 2
    println ("Kill: " + local.killer + " -> " + local.victim)
end

on_weapon_fire:
    local.weapon = parm.get 1
    local.ammo = parm.get 2
    // Track shots fired for accuracy
end

on_weapon_hit:
    local.target = parm.get 1
    local.location = parm.get 2
    // Track hits for accuracy
end
```

### Entity Script Commands
```morpheus
// Player commands
local.player setspeed 300
local.player giveweapon "weapons/mp44.tik"
local.player setteam "allies"

// Actor commands
local.actor anim "walk"
local.actor moveto local.pathnode
local.actor setaccuracy 0.8
local.actor attackplayer

// Vehicle commands
local.tank drivable
local.tank speed 50
local.tank drive local.pathnode
```

---

*Generated from OpenMoHAA source code - January 2026*

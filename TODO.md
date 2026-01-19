# Implemented Script Events

The following script events have been implemented in the OpenMoHAA engine.

## Player Events
- `player_spawn`
- `player_kill`
- `player_death`
- `player_suicide`
- `player_crushed`
- `player_telefragged`
- `player_headshot`
- `player_roadkill`
- `player_bash`
- `player_teamkill`
- `player_jump`
- `player_land`
- `player_crouch`
- `player_prone`
- `player_distance`
- `player_pain`
- `player_drop_weapon`
- `player_switch_weapon`
- `player_spectate`
- `player_freeze`
- `player_kill_score`
- `player_death_score`
- `player_heal`
- `player_chat`
- `player_use`
- `player_use_object_start`
- `player_use_object_finish`
- `player_swim_start`
- `player_swim_end`
- `player_run`
- `player_walk`
- `player_lean_left`
- `player_lean_right`
- `ladder_mount`
- `ladder_dismount`

## Game Flow Events
- `game_start`
- `game_end`
- `round_start`
- `round_end`
- `team_win`
- `game_init`
- `game_check_end`
- `objective_update`

## Weapon Events
- `weapon_fire`
- `weapon_reload`
- `weapon_reload_done`
- `weapon_ready`
- `weapon_no_ammo`
- `weapon_charge`
- `weapon_overheated`
- `weapon_holster`
- `weapon_raise`

## Vehicle & Turret Events
- `vehicle_enter`
- `vehicle_exit`
- `vehicle_death`
- `vehicle_collision`
- `turret_enter`
- `turret_exit`

## Item Events
- `item_pickup`
- `item_respawn`
- `health_pickup`
- `ammo_pickup`
- `armor_pickup`
- `inventory_use`

## World Events
- `door_open`
- `door_close`
- `mover_start`
- `mover_stop`
- `trigger_activate`
- `projectile_spawn`
- `projectile_explode`

## Bot Events
- `bot_spawn`
- `bot_killed`
- `bot_roam` (state entry)
- `bot_curious` (state entry)
- `bot_attack` (state entry)

## Client Events
- `client_connect`
- `client_begin`
- `client_userinfo_changed`
- `client_disconnect`

Total: 90+ events.

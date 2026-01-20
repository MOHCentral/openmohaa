# OpenMoHAA Event Subscribe Reference

This document lists all events that are **actually implemented** in the codebase and can be used with `event_subscribe`.

**Total Implemented Events: 92**

---

## How to Use

Subscribe to events in MorpheusScript:
```
event_subscribe "player_kill" global/my_handler.scr::OnPlayerKill
```

---

## Combat Events (23 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `player_kill` | Player killed another player | attacker, attacker2, victim, inflictor, location, meansofdeath | player.cpp |
| `player_death` | Player died | victim, inflictor | player.cpp |
| `player_damage` | Player took damage | victim, attacker, damage, meansofdeath | player.cpp |
| `player_pain` | Player pain with hit location | victim, attacker, damage, meansofdeath, hit_location | player.cpp |
| `player_headshot` | Headshot kill | attacker, victim, weapon_name | player.cpp, weaputils.cpp |
| `player_suicide` | Player suicide | player | player.cpp |
| `player_crushed` | Player crushed | victim, attacker | player.cpp |
| `player_telefragged` | Player telefragged | victim, attacker | player.cpp |
| `player_roadkill` | Vehicle kill | attacker, victim | player.cpp |
| `player_bash` | Melee bash kill | attacker, victim | player.cpp |
| `player_teamkill` | Team kill | killer, victim | player.cpp |
| `weapon_fire` | Weapon fired | owner, weapon_name, ammo_remaining | weapon.cpp |
| `weapon_hit` | Weapon hit target | owner, target, hit_location_or_type | weaputils.cpp |
| `weapon_reload` | Weapon reloading | owner, weapon_name | weapon.cpp |
| `weapon_reload_done` | Reload complete | owner, weapon_name | weapon.cpp |
| `weapon_change` | Weapon switched | owner, old_weapon, new_weapon, client_num | sentient_combat.cpp |
| `weapon_ready` | Weapon ready | owner, weapon_name | weapon.cpp |
| `weapon_no_ammo` | Out of ammo | owner, weapon_name | weapon.cpp |
| `weapon_holster` | Weapon holstered | owner, weapon_name | weapon.cpp |
| `weapon_raise` | Weapon raised | owner, weapon_name | weapon.cpp |
| `weapon_drop` | Weapon dropped | previous_owner, weapon_entity | weapon.cpp |
| `grenade_throw` | Grenade thrown | owner, projectile_entity | weaputils.cpp |
| `grenade_explode` | Grenade exploded | owner, grenade_entity | weaputils.cpp |

---

## Movement Events (10 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `player_spawn` | Player spawned | player | player.cpp |
| `player_respawn` | Player respawned | player | player.cpp |
| `player_jump` | Player jumped | player | player.cpp |
| `player_land` | Player landed | player, fall_velocity | player.cpp |
| `player_crouch` | Player crouched | player | player.cpp |
| `player_prone` | Player went prone | player | player.cpp |
| `player_stand` | Player stood up | player | player.cpp |
| `player_distance` | Distance traveled | player, walked, sprinted, swam, driven | player.cpp |
| `ladder_mount` | Mounted ladder | player, ladder_entity | player.cpp |
| `ladder_dismount` | Dismounted ladder | player, ladder_entity | player.cpp |

---

## Interaction Events (6 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `player_use` | Player used something | player, target_entity | player.cpp |
| `player_use_object_start` | Started using object | player, use_object | player.cpp |
| `player_use_object_finish` | Finished using object | player, use_object | player.cpp |
| `player_spectate` | Became spectator | player | player.cpp |
| `player_freeze` | Player frozen/unfrozen | player, frozen_state (0 or 1) | player.cpp |
| `player_say` | Chat message | player, message_text | player.cpp |

---

## Item Events (5 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `item_pickup` | Item picked up | player, item_name, amount | item.cpp, weapon.cpp |
| `item_drop` | Item dropped | previous_owner, item_name | item.cpp |
| `item_respawn` | Item respawned on map | item_entity, item_name | item.cpp |
| `health_pickup` | Health picked up | player, heal_amount | health.cpp |
| `ammo_pickup` | Ammo picked up | player, ammo_name, amount | ammo.cpp |

---

## Vehicle/Turret Events (6 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `vehicle_enter` | Entered vehicle | player, vehicle_entity | player.cpp |
| `vehicle_exit` | Exited vehicle | player, vehicle_entity | player.cpp |
| `vehicle_death` | Vehicle destroyed | vehicle, attacker | vehicle.cpp |
| `vehicle_collision` | Vehicle collision | vehicle, other_entity | vehicle.cpp |
| `turret_enter` | Entered turret | player, turret_entity | player.cpp |
| `turret_exit` | Exited turret | player, turret_entity | player.cpp |

---

## Server Events (5 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `server_init` | Game DLL initialized | (none) | g_main.cpp |
| `server_start` | Server startup complete | (none) | g_main.cpp |
| `server_shutdown` | Server shutting down | (none) | g_main.cpp |
| `server_spawned` | Server ready with map | map_name, gametype | g_main.cpp |
| `server_console_command` | Console command executed | command_string | gamecmds.cpp |

---

## Map Events (4 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `map_load_start` | Map load started | map_name | level.cpp |
| `map_load_end` | Map load completed | map_name, gametype | level.cpp |
| `map_change_start` | Map change started | current_map, next_map | g_main.cpp |
| `map_restart` | Map restarted | map_name | g_main.cpp |

---

## Game Flow Events (11 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `game_init` | Game initialized | gametype | dm_manager.cpp |
| `game_start` | Game started | (none) | level.cpp |
| `game_end` | Game ended | (none) | g_main.cpp |
| `round_start` | Round started | (none) | dm_manager.cpp |
| `round_end` | Round ended | (none) | dm_manager.cpp |
| `team_win` | Team won | winning_team_num | dm_manager.cpp |
| `match_end` | Match ended | map_name, gametype, winning_team | g_main.cpp |
| `intermission_start` | Intermission began | map_name, gametype | g_main.cpp |
| `warmup_start` | Warmup period began | map_name | dm_manager.cpp |
| `warmup_end` | Warmup period ended | map_name | dm_manager.cpp |
| `objective_update` | Objective changed | objective_index, new_status | scriptthread.cpp |

---

## Team/Vote Events (5 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `team_join` | Player changed team | player, old_team, new_team | player.cpp, dm_manager.cpp |
| `vote_start` | Vote started | player, vote_name, vote_string | player.cpp |
| `vote_passed` | Vote passed | vote_name, vote_string, yes_count, no_count | level.cpp |
| `vote_failed` | Vote failed | vote_name, fail_reason, yes_count, no_count | level.cpp |
| `objective_capture` | TOW objective captured | objective_entity, controller_team | Tow_Entities.cpp |

---

## Client Events (5 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `client_connect` | Client connected | client_num (NIL for first param) | g_client.cpp |
| `client_begin` | Client began | player | g_client.cpp |
| `client_userinfo_changed` | Userinfo changed | player | g_client.cpp |
| `client_disconnect` | Client disconnected | player | g_client.cpp, player.cpp |
| `player_inactivity_drop` | Dropped for inactivity | player | g_active.cpp |

---

## World Events (3 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `door_open` | Door opened | door_entity, activator | doors.cpp |
| `door_close` | Door closed | door_entity | doors.cpp |
| `explosion` | Explosion occurred | explosion_entity, attacker, damage | explosion.cpp |

---

## AI/Actor Events (7 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `actor_spawn` | AI actor spawned | actor | actor.cpp |
| `actor_killed` | AI actor killed | actor, attacker | actor.cpp |
| `bot_spawn` | Bot spawned | bot_entity | playerbot.cpp |
| `bot_killed` | Bot killed | bot_entity, attacker | playerbot.cpp |
| `bot_roam` | Bot roaming | bot_player | playerbot.cpp |
| `bot_curious` | Bot curious | bot_player | playerbot.cpp |
| `bot_attack` | Bot attacking | bot_player | playerbot.cpp |

---

## Score/Admin Events (2 events)

| Event | Description | Parameters | Source File |
|-------|-------------|------------|-------------|
| `score_change` | Score changed | player, score_type ("kills" or "deaths"), delta, new_total | player.cpp |
| `teamkill_kick` | Kicked for team killing | player, teamkill_count | player.cpp |

---

## Example Usage

### Track Player Kills
```
// In global/stats.scr
main:
    event_subscribe "player_kill" global/stats.scr::OnKill
end

OnKill local.attacker local.attacker2 local.victim local.inflictor local.location local.meansofdeath:
    local.attacker println "You killed " local.victim.targetname " with " local.meansofdeath
end
```

### Track Map Changes
```
// In global/maptracker.scr
main:
    event_subscribe "map_change_start" global/maptracker.scr::OnMapChange
    event_subscribe "server_spawned" global/maptracker.scr::OnServerReady
end

OnMapChange local.current_map local.next_map:
    println "Changing from " local.current_map " to " local.next_map
end

OnServerReady local.map_name local.gametype:
    println "Server ready: " local.map_name " (gametype " local.gametype ")"
end
```

### Track Voting
```
// In global/votelogger.scr
main:
    event_subscribe "vote_start" global/votelogger.scr::OnVoteStart
    event_subscribe "vote_passed" global/votelogger.scr::OnVotePassed
    event_subscribe "vote_failed" global/votelogger.scr::OnVoteFailed
end

OnVoteStart local.player local.vote_name local.vote_string:
    iprintln local.player.targetname " started vote: " local.vote_name " " local.vote_string
end

OnVotePassed local.vote_name local.vote_string local.yes_count local.no_count:
    iprintln "Vote passed (" local.yes_count " yes, " local.no_count " no): " local.vote_string
end

OnVoteFailed local.vote_name local.fail_reason local.yes_count local.no_count:
    iprintln "Vote failed (" local.fail_reason "): " local.yes_count " yes, " local.no_count " no"
end
```

### Track Warmup/Round Flow
```
// In global/roundtracker.scr
main:
    event_subscribe "warmup_start" global/roundtracker.scr::OnWarmupStart
    event_subscribe "warmup_end" global/roundtracker.scr::OnWarmupEnd
    event_subscribe "round_start" global/roundtracker.scr::OnRoundStart
    event_subscribe "intermission_start" global/roundtracker.scr::OnIntermission
end

OnWarmupStart local.map_name:
    iprintln "Warmup started on " local.map_name
end

OnWarmupEnd local.map_name:
    iprintln "Warmup ended - get ready!"
end

OnRoundStart:
    iprintln "Round started!"
end

OnIntermission local.map_name local.gametype:
    iprintln "Match over! Final scores displayed."
end
```

### Track AI Actors
```
// In global/aitracker.scr
main:
    event_subscribe "actor_spawn" global/aitracker.scr::OnActorSpawn
    event_subscribe "actor_killed" global/aitracker.scr::OnActorKilled
end

OnActorSpawn local.actor:
    println "Actor spawned: " local.actor.targetname
end

OnActorKilled local.actor local.attacker:
    if (local.attacker.classname == "player")
        local.attacker println "You killed an enemy!"
    end
end
```

---

*Updated: June 2025*
*Generated from source code analysis of G_ScriptEvent calls*
*All 92 events verified against actual codebase*

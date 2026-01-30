# Implemented Script Events

The following script events have been implemented in the OpenMoHAA engine.

## Player Events
| Event | Status | Parameters | Notes |
|-------|--------|------------|-------|
| `player_spawn` | ✅ | player | Player spawned |
| `player_kill` | ✅ | attacker, attacker, victim, inflictor, location, mod | Player killed another |
| `player_death` | ✅ | player, inflictor | Player died |
| `player_damage` | ✅ | player, attacker, damage, mod | Player took damage |
| `player_pain` | ✅ | player, attacker, damage, mod, location | Player pain with hit location |
| `player_suicide` | ✅ | player | Player suicide |
| `player_crushed` | ✅ | player, attacker | Player crushed |
| `player_telefragged` | ✅ | player, attacker | Player telefragged |
| `player_headshot` | ✅ | attacker, victim, weapon | **Key accuracy stat** |
| `player_roadkill` | ✅ | attacker, victim | Player killed by vehicle |
| `player_bash` | ✅ | attacker, victim | Player bashed |
| `player_teamkill` | ✅ | killer, victim | Player killed teammate |
| `player_jump` | ✅ | player | Player jumped |
| `player_land` | ✅ | player, height | Player landed with fall height |
| `player_crouch` | ✅ | player | Player crouched |
| `player_prone` | ✅ | player | Player went prone |
| `player_distance` | ✅ | player, walked, sprinted, swam, driven | Distance traveled |
| `player_spectate` | ✅ | player | Player became spectator |
| `player_freeze` | ✅ | player, frozen_state | Player frozen/unfrozen |
| `player_say` | ✅ | player, message | Player sent chat message |
| `player_use` | ✅ | player, entity | Player used something |
| `player_use_object_start` | ✅ | player, object | Player started using object |
| `player_use_object_finish` | ✅ | player, object | Player finished using object |
| `ladder_mount` | ✅ | player, ladder | Player mounted ladder |
| `ladder_dismount` | ✅ | player, ladder | Player dismounted ladder |

## Game Flow Events
| Event | Status | Parameters | Notes |
|-------|--------|------------|-------|
| `game_init` | ✅ | gametype | Game initialized |
| `game_start` | ✅ | (none) | Game started (spawn complete) |
| `game_end` | ✅ | (none) | Game ended (intermission began) |
| `round_start` | ✅ | (none) | Round started |
| `round_end` | ✅ | (none) | Round ended |
| `team_win` | ✅ | teamnum | Team won |
| `team_join` | ✅ | player, old_team, new_team | Player changed team |
| `objective_update` | ✅ | index, status | Objective status changed |

## Weapon Events (Accuracy Tracking Ready)
| Event | Status | Parameters | Notes |
|-------|--------|------------|-------|
| `weapon_fire` | ✅ | owner, weapon_name, ammo_left | **Shots fired counter** |
| `weapon_hit` | ✅ | owner, target, location/type | **Hits counter** |
| `weapon_change` | ✅ | owner, old_weapon, new_weapon, client | Weapon switched |
| `weapon_reload` | ✅ | owner, weapon_name | Weapon reloading |
| `weapon_reload_done` | ✅ | owner, weapon_name | Weapon finished reloading |
| `weapon_ready` | ✅ | owner, weapon_name | Weapon raised and ready |
| `weapon_no_ammo` | ✅ | owner, weapon_name | Weapon out of ammo |
| `weapon_holster` | ✅ | owner, weapon_name | Weapon holstered |
| `weapon_raise` | ✅ | owner, weapon_name | Weapon raised |
| `weapon_drop` | ✅ | owner, weapon | Weapon dropped |
| `grenade_throw` | ✅ | owner, projectile | Grenade thrown |
| `grenade_explode` | ✅ | owner, projectile | Grenade exploded |

## Vehicle & Turret Events
| Event | Status | Parameters | Notes |
|-------|--------|------------|-------|
| `vehicle_enter` | ✅ | player, vehicle | Player entered vehicle |
| `vehicle_exit` | ✅ | player, vehicle | Player exited vehicle |
| `vehicle_death` | ✅ | vehicle, attacker | Vehicle destroyed |
| `vehicle_collision` | ✅ | vehicle, other | Vehicle collision |
| `turret_enter` | ✅ | player, turret | Player entered turret |
| `turret_exit` | ✅ | player, turret | Player exited turret |

## Item Events
| Event | Status | Parameters | Notes |
|-------|--------|------------|-------|
| `item_pickup` | ✅ | player, item_name, amount | Universal pickup |
| `item_drop` | ✅ | player, item_name | Item dropped |
| `health_pickup` | ✅ | player, amount | Health picked up |
| `ammo_pickup` | ✅ | player, item_name, amount | Ammo picked up |
| `armor_pickup` | ⚠️ | (via item_pickup) | Use item_pickup with filter |

## World Events
| Event | Status | Parameters | Notes |
|-------|--------|------------|-------|
| `door_open` | ✅ | door, activator | Door opened |
| `door_close` | ✅ | door | Door closed |

## Bot Events
| Event | Status | Parameters | Notes |
|-------|--------|------------|-------|
| `bot_spawn` | ✅ | bot | Bot spawned |
| `bot_killed` | ✅ | bot, attacker | Bot was killed |
| `bot_roam` | ✅ | bot | Bot entered idle/roam state |
| `bot_curious` | ✅ | bot | Bot entered curious state |
| `bot_attack` | ✅ | bot | Bot entered attack state |

## Client Events
| Event | Status | Parameters | Notes |
|-------|--------|------------|-------|
| `client_connect` | ✅ | clientNum | Client connected |
| `client_begin` | ✅ | player | Client began playing |
| `client_userinfo_changed` | ✅ | player | Client userinfo changed |
| `client_disconnect` | ✅ | player | Client disconnected |

## Accuracy Tracking

Calculate player accuracy using these events:

```morpheus
// Track shots fired and hits per player
stats_weapon_fire:
    local.player = parm.self
    local.weapon = parm.get 1
    local.player.shots_fired++
end

stats_weapon_hit:
    local.player = parm.self
    local.target = parm.get 1
    local.location = parm.get 2  // Hit location or "projectile"
    local.player.shots_hit++
end

// Accuracy = (shots_hit / shots_fired) * 100
```

## Legend
- ✅ Fully implemented with ScriptDelegate + G_ScriptEvent call
- ⚠️ Available via another event (use filtering)

**Total: 66 fully implemented events**

# THE GIANT LIST OF OPENMOHAA EVENTS, STATS, AND ACHIEVEMENTS

This document represents the master taxonomy of every possible trackable metric, event variation, and gamified achievement in OpenMoHAA.

**Total Implemented Events: 54**

---

## 1. CORE ENGINE EVENTS (The Source of Truth)

These 54 events are fully implemented and can be subscribed to via `event_subscribe`.

### Core Player Events (6 events)
These are the fundamental player events. All other player-related information (headshots, suicides, teamkills, death types, etc.) can be derived from the parameters of these events.

1. `player_connected` - Player connected to server
2. `player_disconnecting` - Player is disconnecting
3. `player_spawned` - Player spawned/respawned
4. `player_damaged` - Player took damage (includes attacker, damage, location, meansofdeath)
5. `player_killed` - Player was killed (includes attacker, damage, location, meansofdeath)
6. `player_textMessage` - Player sent chat message

**Derivable Information from player_killed:**
- **Headshot:** Check `local.location` (0=head, 1=helmet, 2=neck)
- **Suicide:** Check `local.attacker == self`
- **Teamkill:** Check `local.attacker.dmteam == self.dmteam` and attacker != self
- **Death Type:** Check `local.meansofdeath` (MOD_TELEFRAG, MOD_BASH, MOD_CRUSH, etc.)

### Player Movement Events (4 events)
7. `player_jump` - Player jumped
8. `player_crouch` - Player crouched
9. `player_stand` - Player stood up
10. `player_distance` - Player traveled distance threshold (every 100 units)

### Ladder Events (2 events)
11. `ladder_mount` - Player mounted ladder
12. `ladder_dismount` - Player dismounted ladder

### Weapon Events (12 events)
13. `weapon_fire` - Weapon fired
14. `weapon_hit` - Weapon hit target
15. `weapon_reload` - Started reloading
16. `weapon_reload_done` - Finished reloading
17. `weapon_change` - Switched weapons
18. `weapon_ready` - Weapon ready to fire
19. `weapon_no_ammo` - Weapon out of ammo
20. `weapon_holster` - Weapon holstered
21. `weapon_raise` - Weapon raised
22. `weapon_drop` - Weapon dropped
23. `grenade_throw` - Grenade thrown
24. `grenade_explode` - Grenade exploded

### Item Events (6 events)
25. `item_pickup` - Item picked up
26. `item_drop` - Item dropped
27. `item_respawn` - Item respawned on map
28. `health_pickup` - Health picked up
29. `ammo_pickup` - Ammo picked up
30. `armor_pickup` - Armor picked up

### Team Events (2 events)
31. `team_join` - Player changed teams
32. `team_win` - Team won

### Client Events (1 event)
33. `client_userinfo_changed` - Client userinfo changed

### Game Flow Events (9 events)
34. `game_init` - Game initialized
35. `game_start` - Game started
36. `game_end` - Game ended
37. `round_start` - Round started
38. `round_end` - Round ended
39. `warmup_start` - Warmup started
40. `warmup_end` - Warmup ended
41. `intermission_start` - Intermission started
42. `match_end` - Match ended

### Objective Events (2 events)
43. `objective_update` - Objective status changed
44. `objective_capture` - TOW objective captured

### Map Events (8 events)
45. `map_init` - Map initialization started
46. `map_start` - Map initialization complete
47. `map_shutdown` - Map shutting down
48. `map_ready` - Map ready (entities spawned)
49. `map_load_start` - Map load started
50. `map_load_end` - Map load ended
51. `map_restart` - Map restarted
52. `map_change_start` - Map change started

### Server Events (3 events)
53. `server_console_command` - Console command executed
54. `server_process_start` - Server process started
55. `server_process_quit` - Server process quitting

### Vote Events (3 events)
56. `vote_start` - Vote initiated
57. `vote_passed` - Vote passed
58. `vote_failed` - Vote failed

### Vehicle Events (2 events)
59. `vehicle_death` - Vehicle destroyed
60. `vehicle_collision` - Vehicle collision

### Door Events (2 events)
61. `door_open` - Door opened
62. `door_close` - Door closed

### Bot Events (5 events)
63. `bot_spawn` - Bot spawned
64. `bot_killed` - Bot killed
65. `bot_roam` - Bot roaming
66. `bot_curious` - Bot curious
67. `bot_attack` - Bot attacking

### AI/Actor Events (2 events)
68. `actor_spawn` - AI actor spawned
69. `actor_killed` - AI actor killed

### Misc Events (2 events)
70. `player_inactivity_drop` - Player dropped for inactivity
71. `explosion` - Explosion occurred

---

## 2. DERIVED PLAYER STATISTICS (The "Drill-Down")

This section permutes the 45+ weapons against the action types using data from the events above.

### A. Weapon Mastery (Per-Weapon Stats)
*For EVERY weapon (Colt 45, P38, Webley, Nagant, M1 Garand, Kar98, Springfield, Enfield, SVT40, G43, Thompson, MP40, Sten, PPSH, BAR, MP44, Bazooka...)*

**Combat Efficiency (derived from weapon_fire, weapon_hit, player_killed)**
*   `[Weapon]_Kills`: Total kills (from player_killed where attacker weapon matches)
*   `[Weapon]_Deaths`: Deaths while holding this weapon
*   `[Weapon]_Headshots`: Headshot kills (player_killed with location 0-2)
*   `[Weapon]_Headshot_Percentage`: (Headshots / Kills) %
*   `[Weapon]_Accuracy`: (weapon_hit / weapon_fire) %
*   `[Weapon]_Damage_Dealt`: Total HP damage (from player_damaged)
*   `[Weapon]_Time_Equipped`: Total duration held (from weapon_change events)
*   `[Weapon]_Reloads`: Number of times reloaded (weapon_reload)

### B. Anatomy & Hitbox Statistics
*Derived from player_damaged and player_killed location parameter*
*Locations: Head(0), Helmet(1), Neck(2), Torso(3-5), Pelvis(6), Arms(7-12), Legs(13+)*

*   `HitLoc_Damage_Received_[Part]`: Total damage taken to body part
*   `HitLoc_Damage_Dealt_[Part]`: Total damage dealt to body part
*   `HitLoc_Fatal_Shot_[Part]`: Count of kills where this part was the final hit

### C. Death Type Statistics
*Derived from player_killed meansofdeath parameter*

*   `Deaths_By_Telefrag`: MOD_TELEFRAG
*   `Deaths_By_Crush`: MOD_CRUSH
*   `Deaths_By_Bash`: MOD_BASH/MOD_MELEE
*   `Deaths_By_Explosion`: MOD_EXPLOSION
*   `Suicides`: attacker == victim
*   `Teamkills_Committed`: attacker.team == victim.team

---

## 3. ACHIEVEMENTS & MEDALS (Examples)

### Tier 1: Weapon Training (Bronze/Silver/Gold/Onyx)
*Repeat for all 45 weapons.*
1.  **[Weapon] Marksman**: 100 Kills
2.  **[Weapon] Expert**: 500 Kills
3.  **[Weapon] Master**: 1,000 Kills
4.  **[Weapon] God**: 10,000 Kills
5.  **[Weapon] Surgeon**: 500 Headshots (location 0-2 on player_killed)

### Tier 2: Combat Situations
8.  **Death From Above**: Kill enemy while falling > 10ft
9.  **Grave Digger**: Kill an enemy while you are under 10 HP
10. **Post-Mortem**: Get a grenade kill after you have died
11. **Trade Offer**: Kill an enemy who kills you (Simul-kill)
12. **Wall Hax**: Kill enemy through a door/wall
13. **David vs Goliath**: Kill a Bazooka user with a Pistol
14. **Knife to a Gunfight**: Bash kill vs MG42 user (from player_killed with MOD_BASH)

### Tier 3: Streaks & Multi-Kills
*Derived by tracking time between player_killed events*
22. **Double Kill**: 2 kills in 3 seconds
23. **Triple Kill**: 3 kills in 5 seconds
24. **Multi Kill**: 4 kills in 7 seconds
29. **Killing Spree**: 5 kills without dying
30. **Rampage**: 10 kills without dying
31. **Dominating**: 15 kills without dying
32. **Unstoppable**: 20 kills without dying
33. **Godlike**: 25 kills without dying

### Tier 4: Objective & Teamwork
*Derived from objective_capture, team_win events*
35. **Flag Runner**: Capture 3 objectives in one match
38. **Gatekeeper**: Kill 10 enemies near your objective
43. **Last Man Standing**: Win a round as the sole survivor vs 3+

### Tier 5: Game Flow
*Derived from round_start, round_end, game_end events*
*   **Match MVP**: Highest score when match_end fires
*   **First Blood**: First kill after round_start

### Tier 6: The "Hall of Shame" (Fun/Negative Stats)
*Derived from player_killed*
52. **Kenny**: Die first in every round (first death after round_start)
53. **Suicide King**: 100 self-kills (attacker == victim in player_killed)
54. **Friendly Fire**: Team kill 50 allies

---

## 4. SERVER STATISTICS & GLOBAL RECORDS

These are calculated across ALL players from cumulative event data.

**Global Totals (from weapon_fire, player_killed)**
*   `Global_Bullets_Fired`: Sum of all weapon_fire events
*   `Global_Kills_Map_[Map]`: Kill count per map (from map_ready + player_killed)
*   `Global_Weapon_Popularity`: Usage % based on weapon_fire counts

**Server-Specific Records**
*   `Server_Longest_Match`: Duration from game_start to match_end
*   `Server_Highest_Score`: Max score tracked across match_end events
*   `Server_Most_Kills_One_Game`: Tracking the kill record per game session

---

## 5. EVENT-BASED ACHIEVEMENT GENERATION

Achievements are generated programmatically using this matrix:

**[EVENT] + [CONDITION] + [THRESHOLD]**

**Events:** player_killed, player_damaged, weapon_fire, weapon_hit, objective_capture, etc.

**Conditions:**
*   While specific location hit (headshot from player_killed.location)
*   With specific weapon (from weapon_fire/weapon_change context)
*   Death type (from player_killed.meansofdeath)
*   Self-kill (attacker == victim)
*   Team-kill (attacker.team == victim.team)

**Thresholds:**
*   1, 10, 50, 100, 500, 1000, 10000

**Examples:**
*   701. **Prone Master I**: 10 Kills while prone
*   920. **Kar98 Specialist**: 500 Headshots with Kar98
*   998. **Grandmaster of War**: 1,000,000 Total XP
*   1000. **OpenMoHAA Legend**: Play for 1,000 Hours
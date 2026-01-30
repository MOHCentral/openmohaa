# THE GIANT LIST OF OPENMOHAA EVENTS, STATS, AND ACHIEVEMENTS

This document represents the master taxonomy of every possible trackable metric, event variation, and gamified achievement in OpenMoHAA. It is derived from the engine's atomic event system (`register_event`), weapon definitions (`item.cpp`), and hit location logic (`q_shared.h`).

**Total Estimated Permutations: >5,000+**

---

## 1. ATOMIC ENGINE EVENTS (The Source of Truth)

These 30 events are the building blocks. Every stat below is a filter or aggregation of these.

### Combat Layer
1. `player_kill` (Attacker, Victim, Mod, HitLoc, Weapon)
2. `player_death` (Victim, Inflictor, Mod)
3. `player_damage` (Attacker, Victim, Damage, Loc, Weapon)
4. `player_headshot` (Attacker, Victim, Weapon)
5. `weapon_fire` (Player, Weapon)
6. `weapon_hit` (Player, HitLoc, Surface)
7. `weapon_reload` (Player, Weapon)
8. `weapon_change` (Player, OldWeapon, NewWeapon)
9. `grenade_throw` (Player, Type)
10. `grenade_explode` (Player, DamageRadius)

### Movement Layer
11. `player_jump` (Player, Velocity)
12. `player_land` (Player, Height)
13. `player_crouch` (Player, Duration)
14. `player_prone` (Player, Duration)
15. `player_swim` (Player, Distance)
16. `player_sprint` (Player, Distance)
17. `player_walk` (Player, Distance)
18. `ladder_mount` (Player, LadderID)
19. `ladder_dismount` (Player, LadderID)
20. `wall_touch` (Player, WallNormal)

### Interaction Layer
21. `item_pickup` (Player, ItemName)
22. `item_drop` (Player, ItemName)
23. `player_use` (Player, TargetEntity)
24. `door_open` (Player, DoorID)
25. `turret_mount` (Player, TurretID)

### Session Layer
26. `client_connect` (PlayerIP, GUID)
27. `client_disconnect` (Reason)
28. `client_begin` (Spawn)
29. `team_join` (TeamID)
30. `player_chat` (Message, Team/All)

---

## 2. DERIVED PLAYER STATISTICS (The "Drill-Down")

This section permutes the 45+ weapons against the 30+ action types.

### A. Weapon Mastery (Per-Weapon Stats)
*For EVERY weapon below (Colt 45, P38, Webley, Nagant, M1 Garand, Kar98, Springfield, Enfield, SVT40, G43, Thompson, MP40, Sten, PPSH, BAR, MP44, Bazooka...)*

**Combat Efficiency**
*   `[Weapon]_Kills`: Total kills.
*   `[Weapon]_Deaths`: Deaths while holding this weapon.
*   `[Weapon]_Headshots`: Headshot count.
*   `[Weapon]_Headshot_Percentage`: (Headshots / Kills) %.
*   `[Weapon]_Accuracy`: (Shots Hit / Shots Fired) %.
*   `[Weapon]_Damage_Dealt`: Total HP damage inflicted.
*   `[Weapon]_Damage_Per_Shot`: Average damage per hit.
*   `[Weapon]_Time_Equipped`: Total duration held in hands.
*   `[Weapon]_Reloads`: Number of times reloaded.
*   `[Weapon]_Kills_Per_Mag`: Average kills before reloading.
*   `[Weapon]_Longest_Kill`: Max distance kill.
*   `[Weapon]_Point_Blank_Kills`: Kills < 2 meters.
*   `[Weapon]_Wallbang_Kills`: Kills through geometry.

**Comparison Metrics (Rivals)**
*   `Thompson_vs_MP40_WinRate`: % of duels won against MP40.
*   `Garand_vs_Kar98_WinRate`: % of duels won against Kar98.
*   `Sniper_vs_Sniper_WinRate`: % of duels won against other snipers.
*   `Bazooka_vs_Infantry_Ratio`: Kills vs non-explosive users.

### B. Anatomy & Hitbox Statistics
*Permuted for: Head, Helmet, Neck, Torso(Up/Mid/Low), Pelvis, Arms(L/R), Legs(L/R), Hands, Feet.*

*   `HitLoc_Damage_Received_[Part]`: Total damage taken to body part.
*   `HitLoc_Damage_Dealt_[Part]`: Total damage dealt to body part.
*   `HitLoc_Fatal_Shot_[Part]`: Count of kills where this part was the final hit.
*   `Limb_Amputation_Rate`: (Hypothetical if gore enabled) - Limb hits resulting in gibs.
*   `Groin_Shot_Count`: The "Nutcracker" stat.
*   `Helmet_Pop_Count`: Hits that removed helmet but didn't kill.
*   `Achilles_Heel_Deaths`: Deaths by foot shots.

### C. Movement & Stance Statistics
*   `Stance_Prone_Kills`: Kills while prone.
*   `Stance_Crouch_Kills`: Kills while crouching.
*   `Stance_Air_Kills`: Kills while Z-velocity > 0.
*   `Stance_Transition_Kills`: Kills during crouch/stand animation.
*   `Movement_Sprint_Kills`: Kills while velocity > walk speed.
*   `Movement_Stationary_Kills`: Kills while velocity == 0.
*   `Distance_Traveled_Walk`: Total KM walked.
*   `Distance_Traveled_Sprint`: Total KM sprinted.
*   `Distance_Traveled_Crouch`: Total KM crouched (The "Crab" stat).
*   `Distance_Traveled_Swim`: Total KM swam.
*   `Ladder_Time`: Total seconds on ladders.
*   `Jump_Count`: Total spacebar presses.
*   `Bunny_Hop_Chain_Max`: Max consecutive jumps.

### D. Map-Specific Statistics
*For every map (e.g., mohdm1, mohdm2, obj_team1...)*

*   `Map_[Name]_WinRate`: Win % on this map.
*   `Map_[Name]_KDR`: K/D Ratio specific to this map.
*   `Map_[Name]_Fav_Weapon`: Most used weapon on this map.
*   `Map_[Name]_Heatmap_Zone_A_Kills`: Kills in specific named zones.
*   `Map_[Name]_Spawn_Kills`: Kills near spawn points.
*   `Map_[Name]_Objective_Caps`: Flags/Bombs completed.
*   `Map_[Name]_Fall_Deaths`: Gravity victims on this map.

---

## 3. ACHIEVEMENTS & MEDALS (The 1,000 List)

### Tier 1: Weapon Training (Bronze/Silver/Gold/Onyx)
*Repeat for all 45 weapons.*
1.  **[Weapon] Marksman**: 100 Kills.
2.  **[Weapon] Expert**: 500 Kills.
3.  **[Weapon] Master**: 1,000 Kills.
4.  **[Weapon] God**: 10,000 Kills.
5.  **[Weapon] Surgeon**: 500 Headshots.
6.  **[Weapon] Spray & Pray**: Fire 10,000 rounds.
7.  **[Weapon] Consevator**: 50 Kills with >50% Accuracy.

*(Example subset for specific flavor)*
*   **Tommy Gun Tycoon**: 1,000 Thompson Kills.
*   **Kraut Mower**: 1,000 MP40 Kills.
*   **Garand Thumb**: Reload M1 Garand 500 times empty.
*   **Click-Click-Boom**: Kill with the last bullet in a Kar98 clip.
*   **Potato Masher**: 500 Stielhandgranate Kills.
*   **Pineapple Surprise**: 500 Mk2 Frag Kills.
*   **Bazooka Ace**: 100 Direct Impact Rocket Kills.
*   **Trench Sweeper**: 500 Shotgun Kills.
*   **Silent but Deadly**: 100 Hi-Standard/DeLisle Kills.

### Tier 2: Combat Situations
8.  **Death From Above**: Kill enemy while falling > 10ft.
9.  **Grave Digger**: Kill an enemy while you are under 10 HP.
10. **Post-Mortem**: Get a grenade kill after you have died.
11. **Trade Offer**: Kill an enemy who kills you (Simul-kill).
12. **Blind Fire**: Kill an enemy while 100% blind (Flashbang).
13. **Wall Hax**: Kill enemy through a door/wall.
14. **David vs Goliath**: Kill a Bazooka user with a Pistol.
15. **Knife to a Gunfight**: Bash kill vs MG42 user.
16. **Sniper Duelist**: Headshot a sniper who is scoping you.
17. **Collateral Damage**: Kill 2 enemies with 1 sniper bullet.
18. **Explosive Personality**: Kill 3 enemies with 1 grenade.
19. **Rocket Man**: Kill an enemy while mid-air from a rocket jump.
20. **Door Prize**: Kill someone by crushing them with a door (if physics allow).
21. **Telefrag**: Spawn inside someone and gib them.

### Tier 3: Streaks & Multi-Kills
22. **Double Kill**: 2 kills in 3 seconds.
23. **Triple Kill**: 3 kills in 5 seconds.
24. **Multi Kill**: 4 kills in 7 seconds.
25. **Mega Kill**: 5 kills in 10 seconds.
26. **Ultra Kill**: 6 kills in 12 seconds.
27. **Monster Kill**: 7 kills in 15 seconds.
28. **Ludicrous Kill**: 8+ kills in 20 seconds.
29. **Killing Spree**: 5 kills without dying.
30. **Rampage**: 10 kills without dying.
31. **Dominating**: 15 kills without dying.
32. **Unstoppable**: 20 kills without dying.
33. **Godlike**: 25 kills without dying.
34. **Wicked Sick**: 30 kills without dying.

### Tier 4: Objective & Teamwork
35. **Flag Runner**: Capture 3 flags in one match.
36. **Bomb Squad**: Defuse the bomb with < 1 second left.
37. **Planter**: Plant the bomb 100 times.
38. **Gatekeeper**: Kill 10 enemies near your flag.
39. **Defender**: Return 100 flags.
40. **Medic**: (If health packs drop) Heal 1000 HP.
41. **Ammo Mule**: Resupply teammates 100 times.
42. **Human Shield**: Take 500 damage in a round without dying.
43. **Last Man Standing**: Win a round as the sole survivor vs 3+.

### Tier 5: Movement & Parkour
44. **Marathon Man**: Run 42km total.
45. **Roof Camper**: Spend 50% of a match at highest Z-coords.
46. **Floor Mat**: Spend 50% of a match prone.
47. **Rabbit**: Jump 500 times in one match.
48. **Fish**: Swim 1km total.
49. **Ladder Goat**: Climb 1km vertical distance.
50. **Speed Demon**: Maintain top sprint speed for 60 seconds.

### Tier 6: The "Hall of Shame" (Fun/Negative Stats)
51. **Butterfingers**: Drop the objective flag 10 times.
52. **Kenny**: Die first in every round of a match.
53. **Suicide King**: 100 self-kills (rockets/nades).
54. **Friendly Fire**: Team kill 50 allies.
55. **Broken Legs**: Die from falling damage 50 times.
56. **Fish Food**: Drown 10 times.
57. **Pacifist**: Finish a match with 0 kills and >10 deaths.
58. **Swiss Cheese**: Die from 10 different weapons in one match.
59. **Bot**: Finish with score -5 or lower.
60. **Reload Addict**: Reload with >90% ammo left 1000 times.
61. **AFK**: Be kicked for inactivity 10 times.

### Tier 7: "Sabermetrics" (Advanced Analytics)
62. **The 1%**: Top 1% of Global Elo.
63. **Clutch King**: Highest 1vX win rate on server.
64. **First Blood Ratio**: Highest % of opening kills.
65. **Trade Efficiency**: Best Kill/Death trade ratio.
66. **Accuracy God**: Highest overall accuracy (>40%).
67. **Headshot Machine**: Highest HS% (>50%).
68. **Utility Master**: Most grenade damage per round.
69. **Survivor**: Lowest death rate per minute.
70. **Damage Dealer**: Highest ADR (Average Damage per Round).

---

## 4. SERVER STATISTICS & GLOBAL RECORDS

These are calculated across ALL players.

**Global Totals**
*   `Global_Bullets_Fired`: (e.g., 1,042,912,831)
*   `Global_Distance_Traveled`: Earth circumferences walked.
*   `Global_Kills_Map_[Map]`: Most violent map.
*   `Global_Weapon_Popularity`: Usage % of all weapons.

**Server-Specific Records**
*   `Server_Longest_Match`: Duration record.
*   `Server_Highest_Score`: Max score in one game.
*   `Server_Most_Kills_One_Game`: Tracking the kill record.
*   `Server_Most_Deaths_One_Game`: Tracking the feed record.
*   `Server_Chattiest_Player`: Most messages sent.
*   `Server_Bloodiest_Hour`: Time of day with most kills.

**Meta-Analysis**
*   `Faction_Win_Rate`: Axis vs Allies global win %.
*   `Map_Balance_Index`: How close rounds are on average per map.
*   `Weapon_Balance_Index`: Standard deviation of weapon K/D ratios.

---

## 5. EXTENDED 1,000+ GENERATOR PATTERN

To reach the requested 1,000+ figure practically, the system generates achievements programmatically using this matrix:

**[ACTION] + [CONDITION] + [THRESHOLD]**

**Actions:**
*   Kill, Headshot, Bash, Grenade Kill, Win, Cap, Defuse, Die...

**Conditions:**
*   While Prone
*   While Jumping
*   While Blind
*   While <10HP
*   From >100m
*   From <2m
*   With [Specific Weapon]
*   Against [Specific Weapon]
*   In [Specific Map]
*   Within [Time Limit]

**Thresholds:**
*   1, 10, 50, 100, 500, 1000, 10000

**Examples of Generated List (subset):**
*   ...
*   701. **Prone Master I**: 10 Kills while prone.
*   702. **Prone Master II**: 50 Kills while prone.
*   703. **Prone Master III**: 100 Kills while prone.
*   704. **Airborne I**: 10 Kills while jumping.
*   705. **Airborne II**: 50 Kills while jumping.
*   ...
*   850. **Stalingrad Veteran**: 100 Wins on Stalingrad.
*   851. **V2 Rocket Veteran**: 100 Wins on V2 Rocket.
*   852. **Omaha Beach Veteran**: 100 Wins on Omaha.
*   ...
*   920. **Kar98 Specialist**: 500 Headshots with Kar98.
*   921. **Springfield Specialist**: 500 Headshots with Springfield.
*   922. **Mosin Specialist**: 500 Headshots with Mosin.
*   ...
*   998. **Grandmaster of War**: 1,000,000 Total XP.
*   999. **The Completionist**: Unlock 500 other achievements.
*   1000. **OpenMoHAA Legend**: Play for 1,000 Hours.

*(Full database requires procedural generation in SQL/GameDB based on these patterns)*

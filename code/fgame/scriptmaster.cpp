/*
===========================================================================
Copyright (C) 2023 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// scriptmaster.cpp : Spawns at the beginning of the maps, parse scripts.

#include "g_local.h"
#include "scriptmaster.h"
#include "dapserver.h"
#include "scriptthread.h"
#include "scriptclass.h"
#include "gamescript.h"
#include "scriptvariable.h"
#include "game.h"
#include "g_spawn.h"
#include "object.h"
#include "worldspawn.h"
#include "scriptcompiler.h"
#include "scriptexception.h"

#ifdef USE_MQTT
#include "mqttworker.h"
#endif

#ifdef USE_HTTP
#include "curlworker.h"
#endif

#include <cstdio>

#ifdef WIN32
#    include <direct.h>
#else
#    include <sys/stat.h>
#    include <unistd.h>
#endif

#if defined(CGAME_DLL)

#    include <hud.h>

#    define SCRIPT_Printf  cgi.Printf
#    define SCRIPT_DPrintf cgi.DPrintf

#elif defined(GAME_DLL)

#    include "hud.h"

#    include "dm_manager.h"
#    include "player.h"
#    include "entity.h"
#    include "huddraw.h"
#    include "weaputils.h"
#    include "camera.h"
#    include "consoleevent.h"

#    define SCRIPT_Printf  gi.Printf
#    define SCRIPT_DPrintf gi.DPrintf

#endif

str      vision_current;
qboolean disable_team_change;
qboolean disable_team_spectate;

ScriptMaster Director;

ScriptEvent scriptedEvents[SE_MAX];

CLASS_DECLARATION(Class, ScriptEvent, NULL) {
    {NULL, NULL}
};

//
// world stuff
//
Event EV_SetTiki
(
    "settiki",
    EV_DEFAULT,
    NULL,
    NULL,
    "Not implemented - used to stop script error on dedicated server"
);
Event EV_RegisterAliasAndCache
(
    "aliascache",
    EV_DEFAULT,
    "ssSSSS",
    "alias_name real_name arg1 arg2 arg3 arg4",
    "Sets up an alias and caches the resourse."
);
Event EV_RegisterAlias
(
    "alias",
    EV_DEFAULT,
    "ssSSSS",
    "alias_name real_name arg1 arg2 arg3 arg4",
    "Sets up an alias."
);
Event EV_Cache
(
    "cache",
    EV_CACHE,
    "s",
    "resourceName",
    "pre-cache the given resource."
);

#ifdef USE_HTTP
// Default timeout for curl requests (in seconds)
static int s_curlTimeout = 30;

Event EV_ScriptMaster_CurlGet(
    "curl_get",
    EV_DEFAULT,
    NULL,
    NULL,
    "Performs an HTTP GET request asynchronously.\n"
    "Usage: curl_get url headers callback\n"
    "Calls the callback label with (success, data, http_code)."
);

Event EV_ScriptMaster_CurlPost(
    "curl_post",
    EV_DEFAULT,
    NULL,
    NULL,
    "Performs an HTTP POST request asynchronously.\n"
    "Usage: curl_post url headers data callback\n"
    "Calls the callback label with (success, data, http_code)."
);

Event EV_ScriptMaster_CurlCustom(
    "curl_custom",
    EV_DEFAULT,
    NULL,
    NULL,
    "Performs an HTTP custom request asynchronously.\n"
    "Usage: curl_custom method url headers data callback\n"
    "Calls the callback label with (success, data, http_code)."
);

Event EV_ScriptMaster_CurlSetTimeout(
    "curl_set_timeout",
    EV_DEFAULT,
    "i",
    "timeout",
    "Sets the default timeout for curl requests in seconds."
);
#endif

CLASS_DECLARATION(Listener, ScriptMaster, NULL) {
    {&EV_RegisterAliasAndCache, &ScriptMaster::RegisterAliasAndCache},
    {&EV_RegisterAlias,         &ScriptMaster::RegisterAlias        },
    {&EV_Cache,                 &ScriptMaster::Cache                },
#ifdef USE_HTTP
    {&EV_ScriptMaster_CurlGet,  &ScriptMaster::CurlGet              },
    {&EV_ScriptMaster_CurlPost, &ScriptMaster::CurlPost             },
    {&EV_ScriptMaster_CurlCustom, &ScriptMaster::CurlCustom         },
    {&EV_ScriptMaster_CurlSetTimeout, &ScriptMaster::CurlSetTimeout },
#endif
    {NULL,                      NULL                                }
};

const char *ScriptMaster::ConstStrings[] = {
    "",
    "touch",
    "block",
    "trigger",
    "use",
    "damage",
    "location",
    "say",
    "fail",
    "bump",
    "default",
    "all",
    "move_action",
    "resume",
    "open",
    "close",
    "pickup",
    "reach",
    "start",
    "teleport",
    "move",
    "move_end",
    "moveto",
    "walkto",
    "runto",
    "crouchto",
    "crawlto",
    "stop",
    "reset",
    "prespawn",
    "spawn",
    "playerspawn",
    "skip",
    "roundstart",
    "visible",
    "not_visible",
    "done",
    "animdone",
    "upperanimdone",
    "saydone",
    "flaggedanimdone",
    "idle",
    "walk",
    "shuffle",
    "anim/crouch.scr",
    "forgot",
    "jog_hunch",
    "jog_hunch_rifle",
    "killed",
    "alarm",
    "scriptclass",
    "ai/utils/fact_script_factory.scr",
    "death",
    "death_fall_to_knees",
    "enemy",
    "dead",
    "mood",
    "patrol",
    "runner",
    "follow",
    "action",
    "move_begin",
    "action_begin",
    "action_end",
    "success",
    "entry",
    "exit",
    "path",
    "node",
    "ask_count",
    "attacker",
    "usecover",
    "waitcover",
    "void",
    "end",
    "attack",
    "near",
    "papers",
    "check_papers",
    "timeout",
    "hostile",
    "leader",
    "gamemap",
    "bored",
    "nervous",
    "curious",
    "alert",
    "greet",
    "depend",
    "anim",
    "anim_scripted",
    "anim_curious",
    "animloop",
    "undefined",
    "notset",
    "increment",
    "decrement",
    "toggle",
    "normal",
    "suspend",
    "mystery",
    "surprise",
    "anim/crouch_run.scr",
    "anim/aim.scr",
    "anim/shoot.scr",
    "anim/mg42_shoot.scr",
    "anim/mg42_idle.scr",
    "anim/mg42_reload.scr",
    "drive",
    "global/weapon.scr",
    "global/moveto.scr",
    "global/anim.scr",
    "global/anim_scripted.scr",
    "global/anim_noclip.scr",
    "global/anim_attached.scr",
    "global/walkto.scr",
    "global/runto.scr",
    "aimat",
    "global/disabled_ai.scr",
    "global/crouchto.scr",
    "global/crawlto.scr",
    "global/killed.scr",
    "global/pain.scr",
    "pain",
    "track",
    "hasenemy",
    "anim/cower.scr",
    "anim/stand.scr",
    "anim/idle.scr",
    "anim/surprise.scr",
    "anim/standshock.scr",
    "anim/standidentify.scr",
    "anim/standflinch.scr",
    "anim/dog_idle.scr",
    "anim/dog_attack.scr",
    "anim/dog_curious.scr",
    "anim/dog_chase.scr",
    "cannon",
    "grenade",
    "badplace",
    "heavy",
    "item",
    "items",
    "item1",
    "item2",
    "item3",
    "item4",
    "stand",
    "mg",
    "pistol",
    "rifle",
    "smg",
    "turnto",
    "standing",
    "crouching",
    "prone",
    "offground",
    "walking",
    "running",
    "falling",
    "anim_nothing",
    "anim_direct",
    "anim_path",
    "anim_waypoint",
    "anim_direct_nogravity",
    "emotion_none",
    "emotion_neutral",
    "emotion_worry",
    "emotion_panic",
    "emotion_fear",
    "emotion_disgust",
    "emotion_anger",
    "emotion_aiming",
    "emotion_determined",
    "emotion_dead",
    "emotion_curious",
    "anim/emotion.scr",
    "forceanim",
    "forceanim_scripted",
    "turret",
    "cover",
    "anim/pain.scr",
    "anim/killed.scr",
    "anim/attack.scr",
    "anim/sniper.scr",
    "anim/suppress.scr",
    "knees",
    "crawl",
    "floor",
    "anim/patrol.scr",
    "anim/run.scr",
    "crouch",
    "crouchwalk",
    "crouchrun",
    "anim/crouch_walk.scr",
    "anim/walk.scr",
    "anim/prone.scr",
    "anim/runawayfiring.scr",
    "anim/run_shoot.scr",
    "anim/runto_alarm.scr",
    "anim/runto_casual.scr",
    "anim/runto_cover.scr",
    "anim/runto_danger.scr",
    "anim/runto_dive.scr",
    "anim/runto_flee.scr",
    "anim/runto_inopen.scr",
    "anim/disguise_salute.scr",
    "anim/disguise_wait.scr",
    "anim/disguise_papers.scr",
    "anim/disguise_enemy.scr",
    "anim/disguise_halt.scr",
    "anim/disguise_accept.scr",
    "anim/disguise_deny.scr",
    "anim/cornerleft.scr",
    "anim/cornerright.scr",
    "anim/overattack.scr",
    "anim/lowwall.scr",
    "anim/highwall.scr",
    "anim/continue_last_anim.scr",
    "flagged",
    "anim/fullbody.scr",
    "salute",
    "sentry",
    "officer",
    "rover",
    "none",
    "machinegunner",
    "disguise",
    "dog_idle",
    "dog_attack",
    "dog_curious",
    "dog_grenade",
    "anim/grenadeturn.scr",
    "anim/grenadekick.scr",
    "anim/grenadethrow.scr",
    "anim/grenadetoss.scr",
    "anim/grenademartyr.scr",
    "movedone",
    "aim",
    "ontarget",
    "unarmed",
    "balcony_idle",
    "balcony_curious",
    "balcony_attack",
    "balcony_disguise",
    "balcony_grenade",
    "balcony_pain",
    "balcony_killed",
    "weaponless",
    "death_balcony_intro",
    "death_balcony_loop",
    "death_balcony_outtro",
    "sounddone",
    "noclip",
    "german",
    "american",
    "spectator",
    "freeforall",
    "allies",
    "axis",
    "draw",
    "kills",
    "allieswin",
    "axiswin",
    "anim/say_curious_sight.scr",
    "anim/say_curious_sound.scr",
    "anim/say_grenade_sighted.scr",
    "anim/say_kill.scr",
    "anim/say_mandown.scr",
    "anim/say_sighted.scr",
    "vehicleanimdone",
    "postthink",
    "turndone",
    "anim/no_anim_killed.scr",
    "mg42",
    "mp40",
    // Added in 2.0
    "auto",
    "both",
    // Added in 2.30
    "runandshoot",
    // Added in OPM
    "respawn",
    "viewmodelanim_done"
};

ScriptMaster::~ScriptMaster()
{
#ifdef USE_MQTT
    g_MqttWorker.Stop();
#endif
#ifdef USE_HTTP
    g_CurlWorker.Stop();
    curl_global_cleanup();
#endif
    Reset(false);
}

static int bLoadForMap(char *psMapsBuffer, const char *name)
{
    cvar_t     *mapname = gi.Cvar_Get("mapname", "", 0);
    const char *token;

    if (!strncmp("test", mapname->string, sizeof("test"))) {
        return true;
    }

    token = COM_Parse(&psMapsBuffer);
    if (!token || !*token) {
        Com_Printf("ERROR bLoadForMap: %s alias with empty maps specification.\n", name);
        return false;
    }

    while (token && *token) {
        if (!Q_stricmpn(token, mapname->string, strlen(token))) {
            return true;
        }

        token = COM_Parse(&psMapsBuffer);
    }

    return false;
}

void ScriptMaster::RegisterAliasAndCache(Event *ev)
{
    int      i;
    char     parameters[MAX_STRING_CHARS];
    char    *psMapsBuffer;
    bool     bAlwaysLoaded = false;

    if (ev->NumArgs() < 2) {
        return;
    }

    // Get the parameters for this alias command

    parameters[0] = 0;
    psMapsBuffer  = NULL;

    for (i = 3; i <= ev->NumArgs(); i++) {
        str s;

        // Added in OPM
        //  MOHAA doesn't check that
        if (ev->IsListenerAt(i)) {
            Listener *l = ev->GetListener(i);

            if (l && l == Director.CurrentThread()) {
                s = "local";
            } else {
                s = ev->GetString(i);
            }
        } else {
            s = ev->GetString(i);
        }

        if (!s.icmp("maps")) {
            i++;
            psMapsBuffer = (char *)ev->GetToken(i).c_str();
            continue;
        }

        if (!s.icmp("always")) {
            bAlwaysLoaded = true;
            continue;
        }

        strcat(parameters, s);
        strcat(parameters, " ");
    }

    if (bAlwaysLoaded) {
        gi.GlobalAlias_Add(ev->GetString(1), ev->GetString(2), parameters);
    }

    if (bLoadForMap(psMapsBuffer, ev->GetString(1))) {
        if (!bAlwaysLoaded) {
            gi.GlobalAlias_Add(ev->GetString(1), ev->GetString(2), parameters);
        }

        CacheResource(ev->GetString(2));
    }
}

void ScriptMaster::RegisterAlias(Event *ev)
{
    int      i;
    char     parameters[MAX_STRING_CHARS];
    char    *psMapsBuffer;
    qboolean subtitle;
    bool     bAlwaysLoaded = false;

    if (ev->NumArgs() < 2) {
        return;
    }

    // Get the parameters for this alias command

    parameters[0] = 0;
    subtitle      = 0;
    psMapsBuffer  = NULL;

    for (i = 3; i <= ev->NumArgs(); i++) {
        str s;

        // Added in OPM
        //  MOHAA doesn't check that
        if (ev->IsListenerAt(i)) {
            Listener *l = ev->GetListener(i);

            if (l && l == Director.CurrentThread()) {
                s = "local";
            } else {
                s = ev->GetString(i);
            }
        } else {
            s = ev->GetString(i);
        }

        if (!s.icmp("maps")) {
            i++;
            psMapsBuffer = (char *)ev->GetToken(i).c_str();
            continue;
        }

        if (!s.icmp("always")) {
            bAlwaysLoaded = true;
        } else if (subtitle) {
            strcat(parameters, "\"");
            strcat(parameters, s);
            strcat(parameters, "\" ");

            subtitle = 0;
        } else {
            subtitle = s.icmp("subtitle") == 0;

            strcat(parameters, s);
        }

        strcat(parameters, " ");
    }

    if (bAlwaysLoaded || bLoadForMap(psMapsBuffer, ev->GetString(1))) {
        gi.GlobalAlias_Add(ev->GetString(1), ev->GetString(2), parameters);
    }
}

void ScriptMaster::Cache(Event *ev)
{
    if (!precache->integer) {
        return;
    }

    CacheResource(ev->GetString(1));
}

#ifdef USE_HTTP

static void ExtractHeaders(Event *ev, int argIndex, std::vector<std::string>& headers) {
    if (ev->NumArgs() >= argIndex && !ev->IsNilAt(argIndex)) {
        ScriptVariable& var = ev->GetValue(argIndex);
        gi.Printf("ExtractHeaders: arg %d type=%d\n", argIndex, var.GetType());
        if (var.GetType() == VARIABLE_ARRAY || var.GetType() == VARIABLE_CONSTARRAY) {
             var.CastConstArrayValue();
             int size = var.arraysize();
             gi.Printf("ExtractHeaders: array size=%d\n", size);
             
             // makearray creates nested arrays: each element is [key, value]
             // So headers[1] = ["Content-Type", "application/json"], headers[2] = ["X-Server-Token", "xxx"]
             for (int i = 1; i <= size; i++) {
                 ScriptVariable* elemVar = var[i];
                 if (!elemVar) continue;
                 
                 gi.Printf("ExtractHeaders: element %d type=%d\n", i, elemVar->GetType());
                 
                 // Check if this element is a sub-array (the makearray format)
                 if (elemVar->GetType() == VARIABLE_ARRAY || elemVar->GetType() == VARIABLE_CONSTARRAY) {
                     elemVar->CastConstArrayValue();
                     int subSize = elemVar->arraysize();
                     gi.Printf("ExtractHeaders: sub-array size=%d\n", subSize);
                     
                     if (subSize >= 2) {
                         ScriptVariable* keyVar = (*elemVar)[1];
                         ScriptVariable* valVar = (*elemVar)[2];
                         
                         if (keyVar && valVar) {
                             std::string key = keyVar->stringValue().c_str();
                             std::string value = valVar->stringValue().c_str();
                             if (!key.empty()) {
                                 gi.Printf("ExtractHeaders: header '%s: %s'\n", key.c_str(), value.c_str());
                                 headers.push_back(key + ": " + value);
                             }
                         }
                     }
                 } else {
                     // Flat array format: key1, value1, key2, value2, ...
                     // Process pairs
                     if (i % 2 == 1 && i + 1 <= size) {
                         ScriptVariable* valVar = var[i + 1];
                         if (valVar && valVar->GetType() != VARIABLE_ARRAY && valVar->GetType() != VARIABLE_CONSTARRAY) {
                             std::string key = elemVar->stringValue().c_str();
                             std::string value = valVar->stringValue().c_str();
                             if (!key.empty()) {
                                 gi.Printf("ExtractHeaders: header '%s: %s'\n", key.c_str(), value.c_str());
                                 headers.push_back(key + ": " + value);
                             }
                         }
                     }
                 }
             }
        }
    }
}

void ScriptMaster::CurlGet(Event *ev)
{
    gi.Printf("ScriptMaster::CurlGet: Called with %d args\n", ev->NumArgs());

    if (ev->NumArgs() < 3) {
        throw ScriptException("curl_get requires 3 arguments: url headers callback");
    }

    str url = ev->GetString(1);
    if (Q_stricmpn(url.c_str(), "http://", 7) != 0 && Q_stricmpn(url.c_str(), "https://", 8) != 0) {
        throw ScriptException("curl_get: Invalid URL protocol (must be http or https): %s", url.c_str());
    }

    CurlTask task;
    task.url = url.c_str();
    
    ExtractHeaders(ev, 2, task.headers);

    gi.Printf("ScriptMaster::CurlGet: URL='%s'\n", task.url.c_str());
    if (!task.headers.empty()) {
        gi.Printf("ScriptMaster::CurlGet: Headers:\n");
        for (const auto& header : task.headers) {
            gi.Printf("  %s\n", header.c_str());
        }
    }

    task.callbackLabel = ev->GetString(3).c_str();

    // Get source script from current thread context
    if (Director.CurrentThread()) {
        task.sourceScript = Director.CurrentThread()->FileName();
    }
    
    // Use default timeout
    task.timeout = s_curlTimeout;

    task.isPost = false;
    
    g_CurlWorker.AddTask(task);
}

void ScriptMaster::CurlPost(Event *ev)
{
    gi.Printf("ScriptMaster::CurlPost: Called with %d args\n", ev->NumArgs());

    if (ev->NumArgs() < 4) {
        throw ScriptException("curl_post requires 4 arguments: url headers body callback");
    }

    // Debug: print types of all arguments
    for (int i = 1; i <= ev->NumArgs(); i++) {
        ScriptVariable& var = ev->GetValue(i);
        gi.Printf("  Arg %d: type=%d\n", i, var.GetType());
    }
   
    // Arg 1: URL (string)
    ScriptVariable& urlVar = ev->GetValue(1);
    str url = urlVar.stringValue();
    if (Q_stricmpn(url.c_str(), "http://", 7) != 0 && Q_stricmpn(url.c_str(), "https://", 8) != 0) {
        throw ScriptException("curl_post: Invalid URL protocol (must be http or https): %s", url.c_str());
    }

    CurlTask task;
    task.url = url.c_str();

    // Arg 2: Headers (array)
    ExtractHeaders(ev, 2, task.headers);
    
    // Arg 3: POST data (string)
    ScriptVariable& bodyVar = ev->GetValue(3);
    task.postData = bodyVar.stringValue().c_str();
    
    // Arg 4: Callback label (string)
    ScriptVariable& callbackVar = ev->GetValue(4);
    task.callbackLabel = callbackVar.stringValue().c_str();

    gi.Printf("ScriptMaster::CurlPost: URL='%s'\n", task.url.c_str());
    gi.Printf("ScriptMaster::CurlPost: PostData='%s'\n", task.postData.c_str());
    if (!task.headers.empty()) {
        gi.Printf("ScriptMaster::CurlPost: Headers:\n");
        for (const auto& header : task.headers) {
            gi.Printf("  %s\n", header.c_str());
        }
    }

    // Get source script from current thread context
    if (Director.CurrentThread()) {
        task.sourceScript = Director.CurrentThread()->FileName();
    }
    
    // Use default timeout
    task.timeout = s_curlTimeout;

    task.isPost = true;

    g_CurlWorker.AddTask(task);
}

void ScriptMaster::CurlCustom(Event *ev)
{
    gi.Printf("ScriptMaster::CurlCustom: Called with %d args\n", ev->NumArgs());

    if (ev->NumArgs() < 5) {
        throw ScriptException("curl_custom requires 5 arguments: method url headers body callback");
    }

    str method = ev->GetString(1);
    str url = ev->GetString(2);
    if (Q_stricmpn(url.c_str(), "http://", 7) != 0 && Q_stricmpn(url.c_str(), "https://", 8) != 0) {
        throw ScriptException("curl_custom: Invalid URL protocol (must be http or https): %s", url.c_str());
    }

    CurlTask task;
    task.customMethod = method.c_str();
    task.url = url.c_str();

    ExtractHeaders(ev, 3, task.headers);

    task.postData = ev->GetString(4).c_str();
    task.callbackLabel = ev->GetString(5).c_str();

    // Get source script from current thread context
    if (Director.CurrentThread()) {
        task.sourceScript = Director.CurrentThread()->FileName();
    }

    // Use default timeout
    task.timeout = s_curlTimeout;

    // curl_post and curl_get set isPost automatically, but for custom, we rely on customMethod.
    // However, if postData is present, libcurl might default to POST if we don't handle it carefully.
    // In CurlWorker, we set CUSTOMREQUEST.
    task.isPost = false; // We rely on postData presence in CurlWorker for custom methods

    g_CurlWorker.AddTask(task);
}

void ScriptMaster::CurlSetTimeout(Event *ev)
{
    s_curlTimeout = ev->GetInteger(1);
    if (s_curlTimeout < 1) {
        s_curlTimeout = 1;
    }
    gi.Printf("Curl timeout set to %d seconds\n", s_curlTimeout);
}
#endif

void ScriptMaster::InitConstStrings(void)
{
    EventDef                       *eventDef;
    const_str                       name;
    unsigned int                    eventnum;
    con_map_enum<Event *, EventDef> en;
    int                             i;

    static_assert(ARRAY_LEN(ConstStrings) == (STRING_LENGTH_ - 1), "Constant strings don't match. Make sure the 'const_str' enum match with the 'ConstStrings' string array");

    for (i = 0; (unsigned int)i < ARRAY_LEN(ConstStrings); i++) {
        AddString(ConstStrings[i]);
    }

    if (!Listener::EventSystemStarted) {
        // Added in OPM
        //  This usually means the game module is getting destroyed
        //  most often, the event command list has been destroyed earlier
        return;
    }

    Event::normalCommandList.clear();
    Event::returnCommandList.clear();
    Event::getterCommandList.clear();
    Event::setterCommandList.clear();

    en = Event::eventDefList;

    for (en.NextValue(); en.CurrentValue() != NULL; en.NextValue()) {
        eventDef    = en.CurrentValue();
        eventnum    = (*en.CurrentKey())->eventnum;
        str command = eventDef->command.c_str();

        if (eventDef->type == EV_NORMAL || eventDef->type == EV_RETURN) {
            command.tolower();
        }

        name = AddString(command);

        if (eventDef->type == EV_NORMAL) {
            Event::normalCommandList[name] = eventnum;
        } else if (eventDef->type == EV_RETURN) {
            Event::returnCommandList[name] = eventnum;
        } else if (eventDef->type == EV_GETTER) {
            Event::getterCommandList[name] = eventnum;
        } else if (eventDef->type == EV_SETTER) {
            Event::setterCommandList[name] = eventnum;
        }
    }
}

ScriptThread *ScriptMaster::CreateThread(str filename, str label, Listener *self)
{
    GameScript *scr = GetScript(filename);

    if (!scr) {
        return NULL;
    }

    return CreateThread(scr, label, self);
}

ScriptThread *ScriptMaster::CreateThread(GameScript *scr, str label, Listener *self)
{
    try {
        return CreateScriptThread(scr, self, label);
    } catch (ScriptException& exc) {
        gi.DPrintf("ScriptMaster::CreateThread: %s\n", exc.string.c_str());
        return NULL;
    }
}

void ScriptMaster::ExecuteThread(str filename, str label)
{
    GameScript *scr = GetScript(filename);

    if (!scr) {
        return;
    }

    ExecuteThread(scr, label);
}

void ScriptMaster::ExecuteThread(GameScript *scr, str label)
{
    ScriptThread *thread = CreateThread(scr, label);

    try {
        if (thread) {
            thread->Execute();
        }
    } catch (ScriptException& exc) {
        gi.DPrintf("ScriptMaster::ExecuteThread: %s\n", exc.string.c_str());
    }
}

void ScriptMaster::ExecuteThread(str filename, str label, Event& parms)
{
    GameScript *scr = GetScript(filename);

    if (!scr) {
        return;
    }

    ExecuteThread(scr, label, parms);
}

void ScriptMaster::ExecuteThread(GameScript *scr, str label, Event& parms)
{
    ScriptThread *thread = CreateThread(scr, label);

    try {
        thread->Execute(parms);
    } catch (ScriptException& exc) {
        gi.DPrintf("ScriptMaster::ExecuteThread: %s\n", exc.string.c_str());
    }
}

ScriptThread *ScriptMaster::CreateScriptThread(GameScript *scr, Listener *self, const_str label)
{
    ScriptClass *scriptClass = new ScriptClass(scr, self);

    return CreateScriptThread(scriptClass, label);
}

ScriptThread *ScriptMaster::CreateScriptThread(ScriptClass *scriptClass, unsigned char *m_pCodePos)
{
    return new ScriptThread(scriptClass, m_pCodePos);
}

ScriptThread *ScriptMaster::CreateScriptThread(ScriptClass *scriptClass, const_str label)
{
    unsigned char *m_pCodePos = scriptClass->FindLabel(label);

    if (!m_pCodePos) {
        throw ScriptException(
            "ScriptMaster::CreateScriptThread [DEBUG-CHECK]: label '%s' does not exist in '%s'.",
            Director.GetString(label).c_str(),
            scriptClass->Filename().c_str()
        );
    }

    return CreateScriptThread(scriptClass, m_pCodePos);
}

ScriptThread *ScriptMaster::CreateScriptThread(GameScript *scr, Listener *self, str label)
{
    return CreateScriptThread(scr, self, Director.AddString(label));
}

ScriptThread *ScriptMaster::CreateScriptThread(ScriptClass *scriptClass, str label)
{
    if (label.length() && *label) {
        return CreateScriptThread(scriptClass, Director.AddString(label));
    } else {
        return CreateScriptThread(scriptClass, STRING_EMPTY);
    }
}

ScriptMaster::ScriptMaster()
{
#ifdef USE_MQTT
    g_MqttWorker.Start();
#endif
#ifdef USE_HTTP
    curl_global_init(CURL_GLOBAL_ALL);
    g_CurlWorker.Start();
#endif
}

void ScriptMaster::Reset(qboolean samemap)
{
    ScriptClass_allocator.FreeAll();

    stackCount = 0;
    cmdCount   = 0;
    cmdTime    = 0;
    maxTime    = MAX_EXECUTION_TIME;
    //pTop = &avar_Stack[ 0 ];
    iPaused = 0;

#if defined(GAME_DLL)
    for (int i = 1; i <= m_menus.NumObjects(); i++) {
        Hidemenu(m_menus.ObjectAt(i), true);
    }

    m_menus.ClearObjectList();
#endif

    if (!samemap) {
        for (int i = 0; i < SE_MAX; i++) {
            scriptedEvents[i] = ScriptEvent();
        }

        CloseGameScript();
        StringDict.clear();
        m_scriptCmds.clear();
        InitConstStrings();
    }

    ScriptDelegate::ResetAllDelegates();
}

void ScriptMaster::ExecuteRunning(void)
{
    int i;
    int startTime;
    int startMs;
    str fileName;
    str sourcePosString;

    g_DAPServer.RunFrame();

#ifdef USE_HTTP
    // Process Curl Results
    CurlResult result;
    while (g_CurlWorker.GetResult(result)) {
        if (!result.callbackLabel.empty()) {
            str scriptName = result.sourceScript.empty() ? level.m_mapscript : str(result.sourceScript.c_str());
            str label = result.callbackLabel.c_str();
            GameScript *script = GetScript(scriptName);

            const char *p = strstr(label.c_str(), "::");
            if (p) {
                // Safely extract the script name substring
                scriptName = str(label.c_str(), p - label.c_str());
                // Use a temporary string for the label part to avoid UAF on label buffer
                str labelPart = p + 2;
                label = labelPart;

                script = GetScript(scriptName);
            }

            if (script) {
               ScriptThread* thread = CreateThread(script, label);
               if (thread) {
                   ScriptVariable *parms = new ScriptVariable[3];
                   parms[0].setIntValue(result.success);
                   parms[1].setStringValue(result.data.c_str());
                   parms[2].setIntValue(result.httpCode);
                   thread->Execute(parms, 3);
                   delete[] parms;
               }
            } else {
                 gi.DPrintf("Curl Callback Error: Could not load script '%s'\n", scriptName.c_str());
            }
        }
    }
#endif

#ifdef USE_MQTT
    // Process MQTT Results
    MqttResult mqttResult;
    while (g_MqttWorker.GetResult(mqttResult)) {
        if (!mqttResult.callbackLabel.empty()) {
            str scriptName = mqttResult.sourceScript.empty() ? level.m_mapscript : str(mqttResult.sourceScript.c_str());
            str label = mqttResult.callbackLabel.c_str();
            GameScript *script = GetScript(scriptName);

            const char *p = strstr(label.c_str(), "::");
            if (p) {
                scriptName = str(label.c_str(), p - label.c_str());
                str labelPart = p + 2;
                label = labelPart;
                script = GetScript(scriptName);
            }

            if (script) {
                ScriptThread *thread = CreateThread(script, label);
                if (thread) {
                    if (mqttResult.type == MQTT_RESULT_MESSAGE) {
                        // Subscription message callback: (topic, payload)
                        ScriptVariable *parms = new ScriptVariable[2];
                        parms[0].setStringValue(mqttResult.topic.c_str());
                        parms[1].setStringValue(mqttResult.payload.c_str());
                        thread->Execute(parms, 2);
                        delete[] parms;
                    } else {
                        // Connect/publish/subscribe callback: (success, error_message)
                        ScriptVariable *parms = new ScriptVariable[2];
                        parms[0].setIntValue(mqttResult.success ? 1 : 0);
                        parms[1].setStringValue(mqttResult.errorMessage.c_str());
                        thread->Execute(parms, 2);
                        delete[] parms;
                    }
                }
            } else {
                gi.DPrintf("MQTT Callback Error: Could not load script '%s'\n", scriptName.c_str());
            }
        }
    }
#endif

    if (stackCount) {
        return;
    }

    if (!timerList.IsDirty()) {
        return;
    }

    cmdTime   = 0;
    cmdCount  = 0;
    startTime = level.svsTime;

    int threadCount = 0;
    try {
        while ((m_CurrentThread = (ScriptThread *)timerList.GetNextElement(i))) {
            threadCount++;
            //if (execRunningCount % 100 == 1) {
            //    gi.Printf("ScriptMaster: Found thread #%d at time %d\n", threadCount, i);
            //}
            if (g_timescripts->integer) {
                fileName        = m_CurrentThread->FileName();
                sourcePosString = m_CurrentThread->m_ScriptVM->GetSourcePos();
                startMs         = gi.Milliseconds();
            }

            level.setTime(level.svsStartTime + i);

            m_CurrentThread->m_ScriptVM->m_ThreadState = THREAD_RUNNING;
            //if (execRunningCount % 100 == 1) {
            //    gi.Printf("ScriptMaster: Calling Execute() for thread %p, state=%d\n", 
            //              m_CurrentThread->m_ScriptVM, m_CurrentThread->m_ScriptVM->state);
            //}
            m_CurrentThread->m_ScriptVM->Execute();

            if (g_timescripts->integer) {
                str string;

                string = "Execute Running: ";
                string += str(gi.Milliseconds() - startMs / 1000.f);
                string += " file: ";
                string += fileName;
                string += " codepos: ";
                string += sourcePosString;

                gi.DebugPrintf("%s\n", string.c_str());
            }
        }
    } catch (const ScriptException& e) {
        gi.Error(ERR_DROP, "%s", e.string.c_str());
    }

    level.setTime(startTime);
    level.m_LoopProtection = true;
}

void ScriptMaster::SetTime(int time)
{
    timerList.SetTime(time);
}

void ScriptMaster::AddTiming(ScriptThread *thread, int time)
{
    timerList.AddElement(thread, time);
}

void ScriptMaster::RemoveTiming(ScriptThread *thread)
{
    timerList.RemoveElement(thread);
}

ScriptClass *ScriptMaster::CurrentScriptClass(void)
{
    return CurrentScriptThread()->GetScriptClass();
}

void ScriptMaster::CloseGameScript(void)
{
    con_map_enum<const_str, GameScript *> en(m_GameScripts);
    GameScript                          **g;
    Container<GameScript *>               gameScripts;

    for (g = en.NextValue(); g != NULL; g = en.NextValue()) {
        gameScripts.AddObject(*g);
    }

    for (int i = gameScripts.NumObjects(); i > 0; i--) {
        delete gameScripts.ObjectAt(i);
    }

    m_GameScripts.clear();
}

GameScript *ScriptMaster::GetGameScriptInternal(str& filename)
{
    void       *sourceBuffer = NULL;
    int         sourceLength;
    char        filepath[MAX_QPATH];
    GameScript *scr;

    if (filename.length() >= MAX_QPATH) {
        gi.Error(ERR_DROP, "Script filename '%s' exceeds maximum length of %d\n", filename.c_str(), MAX_QPATH);
    }

    Q_strncpyz(filepath, filename.c_str(), sizeof(filepath));
    gi.FS_CanonicalFilename(filepath);
    filename = filepath;

    scr = m_GameScripts[StringDict.findKeyIndex(filename)];

    if (scr != NULL) {
        return scr;
    }

    scr = new GameScript(filename);

    m_GameScripts[StringDict.addKeyIndex(filename)] = scr;

    if (GetCompiledScript(scr)) {
        scr->m_Filename = Director.AddString(filename);
        return scr;
    }

    sourceLength = gi.FS_ReadFile(filename.c_str(), &sourceBuffer, true);

    if (sourceLength == -1) {
        throw ScriptException("Can't find '%s'\n", filename.c_str());
    }

    scr->Load(sourceBuffer, sourceLength);

    gi.FS_FreeFile(sourceBuffer);

    if (!scr->successCompile) {
        throw ScriptException("Script '%s' was not properly loaded", filename.c_str());
    }

    return scr;
}

GameScript *ScriptMaster::GetGameScript(const_str filename, qboolean recompile)
{
    return GetGameScript(Director.GetString(filename), recompile);
}

GameScript *ScriptMaster::GetGameScript(str filename, qboolean recompile)
{
    const_str   s   = StringDict.findKeyIndex(filename);
    GameScript *scr = m_GameScripts[s];
    int         i;

    if (scr != NULL && !recompile) {
        if (!scr->successCompile) {
            throw ScriptException("Script '%s' was not properly loaded\n", filename.c_str());
        }

        return scr;
    } else {
        if (scr && recompile) {
            Container<ScriptClass *>         list;
            MEM_BlockAlloc_enum<ScriptClass> en = ScriptClass_allocator;
            ScriptClass                     *scriptClass;
            m_GameScripts[s] = NULL;

            for (scriptClass = en.NextElement(); scriptClass != NULL; scriptClass = en.NextElement()) {
                if (scriptClass->GetScript() == scr) {
                    list.AddObject(scriptClass);
                }
            }

            for (i = 1; i <= list.NumObjects(); i++) {
                delete list.ObjectAt(i);
            }

            delete scr;
        }

        return GetGameScriptInternal(filename);
    }
}

GameScript *ScriptMaster::GetScript(const_str filename, qboolean recompile)
{
    try {
        return GetGameScript(filename, recompile);
    } catch (ScriptException& exc) {
        gi.DPrintf("ScriptMaster::GetScript: %s\n", exc.string.c_str());
    }

    return NULL;
}

GameScript *ScriptMaster::GetScript(str filename, qboolean recompile)
{
    try {
        return GetGameScript(filename, recompile);
    } catch (ScriptException& exc) {
        gi.DPrintf("ScriptMaster::GetScript: %s\n", exc.string.c_str());
    }

    return NULL;
}

void ScriptMaster::ArchiveString(Archiver& arc, const_str& s)
{
    str  s2;
    byte b;

    if (arc.Loading()) {
        arc.ArchiveByte(&b);
        if (b) {
            arc.ArchiveString(&s2);
            s = AddString(s2);
        } else {
            s = 0;
        }
    } else {
        if (s) {
            b = 1;
            arc.ArchiveByte(&b);

            s2 = Director.GetString(s);
            arc.ArchiveString(&s2);
        } else {
            b = 0;
            arc.ArchiveByte(&b);
        }
    }
}

void ScriptMaster::Archive(Archiver& arc)
{
    int          count;
    int          i, j;
    ScriptClass *c;
    int          num;
    ScriptVM    *scriptVM;
    ScriptVM    *prevScriptVM;

    if (arc.Saving()) {
        count = (int)ScriptClass_allocator.Count();
        arc.ArchiveInteger(&count);

        MEM_BlockAlloc_enum<ScriptClass> en = ScriptClass_allocator;
        for (c = en.NextElement(); c != NULL; c = en.NextElement()) {
            c->ArchiveInternal(arc);

            num = 0;
            for (scriptVM = c->m_Threads; scriptVM != NULL; scriptVM = scriptVM->next) {
                num++;
            }

            arc.ArchiveInteger(&num);

            for (scriptVM = c->m_Threads; scriptVM != NULL; scriptVM = scriptVM->next) {
                scriptVM->m_Thread->ArchiveInternal(arc);
            }
        }
    } else {
        arc.ArchiveInteger(&count);

        for (i = 0; i < count; i++) {
            c = new ScriptClass();
            c->ArchiveInternal(arc);

            arc.ArchiveInteger(&num);

            c->m_Threads = NULL;
            prevScriptVM = NULL;

            for (j = 0; j < num; j++) {
                scriptVM                       = new ScriptVM;
                scriptVM->m_Thread             = new ScriptThread;
                scriptVM->m_Thread->m_ScriptVM = scriptVM;
                scriptVM->m_ScriptClass        = c;
                scriptVM->next                 = NULL;

                if (prevScriptVM) {
                    prevScriptVM->next = scriptVM;
                } else {
                    c->m_Threads = scriptVM;
                }

                prevScriptVM = scriptVM;
                scriptVM->m_Thread->ArchiveInternal(arc);
            }
        }
    }

    timerList.Archive(arc);
    m_menus.Archive(arc);
}

GameScript *ScriptMaster::GetTempScript(const char *data)
{
    GameScript *scr = new GameScript;

    scr->Load((void *)data, strlen(data));

    if (!scr->successCompile) {
        return NULL;
    }

    return scr;
}

void ScriptMaster::AddMenu(str name)
{
    m_menus.AddUniqueObject(name);
}

void ScriptMaster::RemoveMenu(str name)
{
    if (m_menus.IndexOfObject(name)) {
        m_menus.RemoveObject(name);
    }
}

void ScriptMaster::LoadMenus(void)
{
    for (int i = 1; i <= m_menus.NumObjects(); i++) {
        Showmenu(m_menus.ObjectAt(i), true);
    }
}

ScriptThread *ScriptMaster::PreviousThread(void)
{
    return m_PreviousThread;
}

ScriptThread *ScriptMaster::CurrentThread(void)
{
    return m_CurrentThread;
}

ScriptThread *ScriptMaster::CurrentScriptThread(void)
{
    if (!m_CurrentThread) {
        ScriptError("current thread is NULL");
    }

    return m_CurrentThread;
}

const_str ScriptMaster::AddString(const char *s)
{
    return StringDict.addKeyIndex(s);
}

const_str ScriptMaster::AddString(str& s)
{
    return StringDict.addKeyIndex(s);
}

const_str ScriptMaster::GetString(const char *s)
{
    const_str cs = StringDict.findKeyIndex(s);

    return cs ? cs : STRING_EMPTY;
}

const_str ScriptMaster::GetString(str s)
{
    return GetString(s.c_str());
}

str& ScriptMaster::GetString(const_str s)
{
    return StringDict[s];
}

void ScriptMaster::Pause()
{
    iPaused++;
}

void ScriptMaster::Unpause()
{
    iPaused--;
    if (iPaused == 0) {
        ExecuteRunning();
    }
}

void ScriptMaster::AllowPause(bool allow)
{
    if (allow) {
        iPaused = 0;
    } else {
        iPaused = -1;
    }
}

void ScriptMaster::PrintStatus(void)
{
    str                              status;
    int                              iThreadNum       = 0;
    int                              iThreadRunning   = 0;
    int                              iThreadWaiting   = 0;
    int                              iThreadSuspended = 0;
    MEM_BlockAlloc_enum<ScriptClass> en               = ScriptClass_allocator;
    ScriptClass                     *scriptClass;
    char                             szBuffer[MAX_STRING_TOKENS];

    status = "num     state      label           script         \n";
    status += "------- ---------- --------------- ---------------\n";

    for (scriptClass = en.NextElement(); scriptClass != NULL; scriptClass = en.NextElement()) {
        ScriptVM *vm;

        for (vm = scriptClass->m_Threads; vm != NULL; vm = vm->next) {
            Com_sprintf(szBuffer, sizeof(szBuffer), "%.7d", iThreadNum);
            status += szBuffer + str(" ");

            switch (vm->ThreadState()) {
            case THREAD_RUNNING:
                Com_sprintf(szBuffer, sizeof(szBuffer), "%8s", "running");
                iThreadRunning++;
                break;
            case THREAD_WAITING:
                Com_sprintf(szBuffer, sizeof(szBuffer), "%8s", "waiting");
                iThreadWaiting++;
                break;
            case THREAD_SUSPENDED:
                Com_sprintf(szBuffer, sizeof(szBuffer), "%8s", "suspended");
                iThreadSuspended++;
                break;
            }

            status += szBuffer;

            Com_sprintf(szBuffer, sizeof(szBuffer), "%15s", vm->Label().c_str());
            status += szBuffer + str(" ");

            Com_sprintf(szBuffer, sizeof(szBuffer), "%15s", vm->Filename().c_str());
            status += szBuffer;

            status += "\n";
            iThreadNum++;
        }
    }

    status += "------- ---------- --------------- ---------------\n";
    status += str(m_GameScripts.size()) + " total scripts compiled\n";
    status += str(iThreadNum) + " total threads ( " + str(iThreadRunning) + " running thread(s), " + str(iThreadWaiting)
            + " waiting thread(s), " + str(iThreadSuspended) + " suspended thread(s) )\n";

    gi.Printf(status.c_str());
}

void ScriptMaster::PrintThread(int iThreadNum)
{
    int                              iThread = 0;
    ScriptVM                        *vm;
    bool                             bFoundThread = false;
    str                              status;
    MEM_BlockAlloc_enum<ScriptClass> en = ScriptClass_allocator;
    ScriptClass                     *scriptClass;

    for (scriptClass = en.NextElement(); scriptClass != NULL; scriptClass = en.NextElement()) {
        for (vm = scriptClass->m_Threads; vm != NULL; vm = vm->next) {
            if (iThread == iThreadNum) {
                bFoundThread = true;
                break;
            }

            iThread++;
        }

        if (bFoundThread) {
            break;
        }
    }

    if (!bFoundThread) {
        gi.Printf("Can't find thread id %i.\n", iThreadNum);
    }

    status = "-------------------------\n";
    status += "num: " + str(iThreadNum) + "\n";

    switch (vm->ThreadState()) {
    case THREAD_RUNNING:
        status += "state: running\n";
        break;
    case THREAD_WAITING:
        status += "state: waiting\n";
        break;
    case THREAD_SUSPENDED:
        status += "state: suspended\n";
        break;
    }

    status += "script: '" + vm->Filename() + "'\n";
    status += "label: '" + vm->Label() + "'\n";
    status += "waittill: ";

    if (!vm->m_Thread->m_WaitForList) {
        status += "(none)\n";
    } else {
        con_set_enum<const_str, ConList>    waitListEnum = *vm->m_Thread->m_WaitForList;
        con_set<const_str, ConList>::Entry *entry;
        int                                 i = 0;

        for (entry = waitListEnum.NextElement(); entry != NULL; entry = waitListEnum.NextElement()) {
            str& name = Director.GetString(entry->GetKey());

            if (i > 0) {
                status += ", ";
            }

            status += "'" + name + "'";

            for (int j = 1; j <= entry->value.NumObjects(); j++) {
                Listener *l = entry->value.ObjectAt(j);

                if (j > 1) {
                    status += ", ";
                }

                if (l) {
                    status += " on " + str(l->getClassname());
                } else {
                    status += " on (null)";
                }
            }

            i++;
        }

        status += "\n";
    }

    gi.Printf(status.c_str());
}

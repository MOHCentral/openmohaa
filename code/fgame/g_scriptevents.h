#pragma once

#include "scriptdelegate.h"
#include "entity.h"
#include <string>

// Helper overloads for G_ScriptEventArg
inline void G_ScriptEventArg(Event& ev, int val) { ev.AddInteger(val); }
inline void G_ScriptEventArg(Event& ev, float val) { ev.AddFloat(val); }
inline void G_ScriptEventArg(Event& ev, double val) { ev.AddFloat((float)val); }
inline void G_ScriptEventArg(Event& ev, const char* val) { ev.AddString(val); }
inline void G_ScriptEventArg(Event& ev, const std::string& val) { ev.AddString(val.c_str()); }
inline void G_ScriptEventArg(Event& ev, const Vector& val) { ev.AddVector(val); }
inline void G_ScriptEventArg(Event& ev, Entity* val) { ev.AddEntity(val); }

// Templated G_ScriptEvent using C++17 fold expressions
template<typename... Args>
void G_ScriptEvent(const char* eventName, Entity* entity, Args&&... args)
{
    ScriptDelegate* delegate = ScriptDelegate::GetScriptDelegate(eventName);
    if (!delegate) {
        // Warning?
        return;
    }

    Event ev;
    (G_ScriptEventArg(ev, std::forward<Args>(args)), ...);

    // Trigger
    if (entity) {
        delegate->Trigger(static_cast<Listener*>(entity), ev);
    } else {
        delegate->Trigger(ev);
    }
}

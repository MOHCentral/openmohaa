#pragma once

#include "scriptdelegate.h"

// Helper function to trigger events easily
// Format specifiers:
// s: string
// i: integer
// f: float
// v: vector
// e: entity
void G_ScriptEvent(const char* eventName, Entity* entity, const char* format, ...);

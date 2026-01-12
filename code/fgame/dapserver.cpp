/*
===========================================================================
Copyright (C) 2025 the OpenMoHAA team

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

#include "g_local.h"
#include "dapserver.h"
#include "scriptmaster.h"
#include "../script/scriptvm.h"
#include "gamescript.h"
#include "../script/scriptclass.h"
#include "level.h"
#include "game.h"
#include "entity.h"
#include <iostream>
#include <sstream>
#include <chrono>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#define closesocket close
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

DAPServer g_DAPServer;
DAPServer* DAPServer::s_Instance = &g_DAPServer;

DAPServer& DAPServer::Get() {
    return g_DAPServer;
}

DAPServer::DAPServer()
    : m_Running(false)
    , m_DebuggerAttached(false)
    , m_ServerSocket(INVALID_SOCKET)
    , m_ClientSocket(INVALID_SOCKET)
{
}

DAPServer::~DAPServer() {
    Stop();
}

void DAPServer::Start(int port) {
    if (m_Running) return;

    m_Running = true;
    m_NetworkThread = std::thread([this, port]() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        m_ServerSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_ServerSocket == INVALID_SOCKET) {
            gi.DPrintf("DAP: Failed to create socket\n");
            return;
        }

        int opt = 1;
        setsockopt(m_ServerSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(m_ServerSocket, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
            gi.DPrintf("DAP: Failed to bind to port %d\n", port);
            closesocket(m_ServerSocket);
            return;
        }

        if (listen(m_ServerSocket, 1) == SOCKET_ERROR) {
            gi.DPrintf("DAP: Failed to listen\n");
            closesocket(m_ServerSocket);
            return;
        }

        gi.Printf("DAP Server listening on port %d\n", port);

        NetworkLoop();

        if (m_ServerSocket != INVALID_SOCKET) {
            closesocket(m_ServerSocket);
            m_ServerSocket = INVALID_SOCKET;
        }

#ifdef _WIN32
        WSACleanup();
#endif
    });
}

void DAPServer::Stop() {
    m_Running = false;
    if (m_ServerSocket != INVALID_SOCKET) {
#ifdef _WIN32
        shutdown(m_ServerSocket, SD_BOTH);
#else
        shutdown(m_ServerSocket, SHUT_RDWR);
#endif
        closesocket(m_ServerSocket);
        m_ServerSocket = INVALID_SOCKET;
    }
    if (m_ClientSocket != INVALID_SOCKET) {
        closesocket(m_ClientSocket);
        m_ClientSocket = INVALID_SOCKET;
    }

    if (m_NetworkThread.joinable()) {
        m_NetworkThread.join();
    }
}

int DAPServer::RegisterVM(ScriptVM* vm) {
    std::lock_guard<std::mutex> lock(m_VMLock);
    if (m_VMToID.find(vm) != m_VMToID.end()) {
        return m_VMToID[vm];
    }
    int id = m_NextVMID++;
    m_IDToVM[id] = vm;
    m_VMToID[vm] = id;
    return id;
}

void DAPServer::UnregisterVM(ScriptVM* vm) {
    std::lock_guard<std::mutex> lock(m_VMLock);
    if (m_VMToID.find(vm) != m_VMToID.end()) {
        int id = m_VMToID[vm];
        m_IDToVM.erase(id);
        m_VMToID.erase(vm);
        m_ThreadStates.erase(vm);
    }
}

ScriptVM* DAPServer::GetVM(int id) {
    std::lock_guard<std::mutex> lock(m_VMLock);
    auto it = m_IDToVM.find(id);
    if (it != m_IDToVM.end()) {
        return it->second;
    }
    return nullptr;
}

void DAPServer::NetworkLoop() {
    while (m_Running) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);

        socket_t client = accept(m_ServerSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (client == INVALID_SOCKET) {
            if (m_Running) {
                // Yield and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        gi.Printf("DAP: Debugger connected\n");
        m_ClientSocket = client;

        // Set non-blocking for client socket if needed, but blocking is fine for simple loop

        std::string buffer;
        char tempBuf[4096];

        while (m_Running && m_ClientSocket != INVALID_SOCKET) {
            // Check for outgoing messages first
            {
                std::lock_guard<std::mutex> lock(m_QueueLock);
                while (!m_OutgoingQueue.empty()) {
                    std::string msg = m_OutgoingQueue.front();
                    m_OutgoingQueue.pop();
                    send(m_ClientSocket, msg.c_str(), msg.length(), 0);
                }
            }

            // Use select to wait for data with timeout so we can check m_Running and outgoing queue
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(m_ClientSocket, &readfds);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 50000; // 50ms

            int activity = select((int)m_ClientSocket + 1, &readfds, NULL, NULL, &tv);

            if (activity > 0 && FD_ISSET(m_ClientSocket, &readfds)) {
                int bytesRead = recv(m_ClientSocket, tempBuf, sizeof(tempBuf) - 1, 0);
                if (bytesRead <= 0) {
                    gi.Printf("DAP: Debugger disconnected\n");
                    closesocket(m_ClientSocket);
                    m_ClientSocket = INVALID_SOCKET;
                    m_DebuggerAttached = false;
                    break;
                }

                tempBuf[bytesRead] = '\0';
                buffer.append(tempBuf);

                // Process buffer for Content-Length headers
                while (true) {
                    size_t headerEnd = buffer.find("\r\n\r\n");
                    if (headerEnd == std::string::npos) break;

                    std::string header = buffer.substr(0, headerEnd);
                    size_t contentLengthPos = header.find("Content-Length: ");
                    if (contentLengthPos == std::string::npos) {
                        // Invalid header, discard?
                        buffer.erase(0, headerEnd + 4);
                        continue;
                    }

                    int contentLength = std::stoi(header.substr(contentLengthPos + 16));
                    if (buffer.length() < headerEnd + 4 + contentLength) {
                        break; // Wait for more data
                    }

                    std::string content = buffer.substr(headerEnd + 4, contentLength);
                    buffer.erase(0, headerEnd + 4 + contentLength);

                    try {
                        json j = json::parse(content);
                        std::lock_guard<std::mutex> lock(m_QueueLock);
                        m_IncomingQueue.push(j);
                    } catch (const std::exception& e) {
                        gi.DPrintf("DAP: JSON Parse Error: %s\n", e.what());
                    }
                }
            }
        }
    }
}

void DAPServer::RunFrame() {
    if (!m_Running) return;

    std::queue<json> queueToProcess;
    {
        std::lock_guard<std::mutex> lock(m_QueueLock);
        std::swap(queueToProcess, m_IncomingQueue);
    }

    while (!queueToProcess.empty()) {
        ProcessMessage(queueToProcess.front());
        queueToProcess.pop();
    }
}

void DAPServer::ProcessMessage(const json& msg) {
    if (!msg.contains("type") || msg["type"] != "request") return;

    std::string command = msg["command"];

    if (command == "initialize") HandleInitialize(msg);
    else if (command == "launch") HandleLaunch(msg);
    else if (command == "attach") HandleAttach(msg);
    else if (command == "setBreakpoints") HandleSetBreakpoints(msg);
    else if (command == "configurationDone") HandleConfigurationDone(msg);
    else if (command == "threads") HandleThreads(msg);
    else if (command == "stackTrace") HandleStackTrace(msg);
    else if (command == "scopes") HandleScopes(msg);
    else if (command == "variables") HandleVariables(msg);
    else if (command == "continue") HandleContinue(msg);
    else if (command == "next") HandleNext(msg);
    else if (command == "stepIn") HandleStepIn(msg);
    else if (command == "stepOut") HandleStepOut(msg);
    else if (command == "pause") HandlePause(msg);
    else {
        // Unknown request
        SendResponse(msg, json::object(), false, "Unknown command");
    }
}

void DAPServer::SendResponse(const json& req, const json& body, bool success, const std::string& message) {
    json res;
    res["type"] = "response";
    res["request_seq"] = req["seq"];
    res["command"] = req["command"];
    res["success"] = success;
    if (!success) res["message"] = message;
    res["body"] = body;
    res["seq"] = 0; // Will be ignored by us usually, or we should track seq

    std::string content = res.dump();
    std::string packet = "Content-Length: " + std::to_string(content.length()) + "\r\n\r\n" + content;

    std::lock_guard<std::mutex> lock(m_QueueLock);
    m_OutgoingQueue.push(packet);
}

void DAPServer::SendEvent(const std::string& event, const json& body) {
    json evt;
    evt["type"] = "event";
    evt["event"] = event;
    evt["body"] = body;
    evt["seq"] = 0;

    std::string content = evt.dump();
    std::string packet = "Content-Length: " + std::to_string(content.length()) + "\r\n\r\n" + content;

    std::lock_guard<std::mutex> lock(m_QueueLock);
    m_OutgoingQueue.push(packet);
}

// Handlers

void DAPServer::HandleInitialize(const json& req) {
    json body;
    body["supportsConfigurationDoneRequest"] = true;
    body["supportsFunctionBreakpoints"] = false;
    body["supportsConditionalBreakpoints"] = false;
    body["supportsHitConditionBreakpoints"] = false;
    body["supportsEvaluateForHovers"] = false;
    body["exceptionBreakpointFilters"] = json::array();

    SendResponse(req, body);

    SendEvent("initialized");
}

void DAPServer::HandleLaunch(const json& req) {
    m_DebuggerAttached = true;
    SendResponse(req);
}

void DAPServer::HandleAttach(const json& req) {
    m_DebuggerAttached = true;
    SendResponse(req);
}

void DAPServer::HandleConfigurationDone(const json& req) {
    UpdateBreakpoints();
    SendResponse(req);
}

void DAPServer::HandleSetBreakpoints(const json& req) {
    std::string file = req["arguments"]["source"]["path"];

    // Normalize file path (basic)
    // In real usage we might need better path normalization to match game script paths
    // For now assume user sends paths that match what we return or relative paths

    m_RequestedBreakpoints[file].clear();

    json breakpoints = json::array();

    if (req["arguments"].contains("breakpoints")) {
        int id = 1;
        for (const auto& bp : req["arguments"]["breakpoints"]) {
            int line = bp["line"];

            BreakpointInfo info;
            info.file = file;
            info.line = line;
            info.id = id++;
            info.verified = false;

            m_RequestedBreakpoints[file].push_back(info);

            json resBp;
            resBp["verified"] = false; // Will be verified in UpdateBreakpoints
            resBp["line"] = line;
            breakpoints.push_back(resBp);
        }
    }

    UpdateBreakpoints();

    // Re-verify breakpoints for response
    json responseBody;
    json actualBreakpoints = json::array();
    for (const auto& bp : m_RequestedBreakpoints[file]) {
        json resBp;
        resBp["verified"] = bp.verified;
        resBp["line"] = bp.line;
        actualBreakpoints.push_back(resBp);
    }
    responseBody["breakpoints"] = actualBreakpoints;

    SendResponse(req, responseBody);
}

void DAPServer::UpdateBreakpoints() {
    m_ActiveBreakpoints.clear();

    // Iterate all loaded scripts and try to match breakpoints
    // This is expensive so we do it only when breakpoints change or scripts load
    // For now we do it when requested.

    con_map_enum<const_str, GameScript *> en(Director.m_GameScripts);
    GameScript **g;

    for (g = en.NextValue(); g != NULL; g = en.NextValue()) {
        GameScript* script = *g;
        MapBreakpointsForScript(script);
    }
}

void DAPServer::MapBreakpointsForScript(GameScript* script) {
    if (!script) return;

    str filename = script->Filename();
    std::string scriptFile = filename.c_str();

    // Find matching requested breakpoints
    // We need fuzzy matching because IDE path might be full path, game path is relative

    std::vector<BreakpointInfo>* requested = nullptr;

    for (auto& pair : m_RequestedBreakpoints) {
        std::string reqFile = pair.first;
        // Simple check: does reqFile end with scriptFile?
        // Or convert both to unified separators and check
        std::replace(reqFile.begin(), reqFile.end(), '\\', '/');
        std::replace(scriptFile.begin(), scriptFile.end(), '\\', '/');

        if (reqFile.find(scriptFile) != std::string::npos || scriptFile.find(reqFile) != std::string::npos) {
            requested = &pair.second;
            break;
        }
    }

    if (!requested) return;

    // Iterate script source map
    if (script->m_ProgToSource) {
        con_set_enum<const uchar *, sourceinfo_t> en(*script->m_ProgToSource);
        con_set<const uchar *, sourceinfo_t>::Entry *entry;

        for (entry = en.NextElement(); entry != NULL; entry = en.NextElement()) {
            int line = entry->value.line;
            unsigned char* code = (unsigned char*)entry->GetKey();

            for (auto& bp : *requested) {
                if (bp.line == line) {
                    m_ActiveBreakpoints[code] = bp.id;
                    bp.verified = true;
                }
            }
        }
    }
}

void DAPServer::HandleThreads(const json& req) {
    json threads = json::array();

    // Iterate active threads
    int count = 0;

    // We iterate ScriptClass_allocator to find all VMs
    MEM_BlockAlloc_enum<ScriptClass> en = ScriptClass_allocator;
    ScriptClass *scriptClass;

    for (scriptClass = en.NextElement(); scriptClass != NULL; scriptClass = en.NextElement()) {
        ScriptVM *vm;
        for (vm = scriptClass->m_Threads; vm != NULL; vm = vm->next) {
            json t;
            int id = RegisterVM(vm);
            t["id"] = id;
            std::string name = vm->Filename().c_str();
            name += ":";
            name += vm->Label().c_str();
            t["name"] = name;
            threads.push_back(t);
            count++;
        }
    }

    json body;
    body["threads"] = threads;
    SendResponse(req, body);
}

void DAPServer::HandleStackTrace(const json& req) {
    long long threadId = req["arguments"]["threadId"];
    ScriptVM* vm = GetVM((int)threadId);

    if (!vm) {
        SendResponse(req, json::object(), false, "Thread not found");
        return;
    }

    json stackFrames = json::array();

    // Current frame
    {
        json frame;
        frame["id"] = 0;
        frame["name"] = vm->Label().c_str();

        json source;
        source["path"] = vm->Filename().c_str();
        frame["source"] = source;

        // Get line number
        str sourceLine;
        int col, line;
        if (vm->GetScript()->GetSourceAt(vm->m_CodePos, &sourceLine, col, line)) {
            frame["line"] = line;
            frame["column"] = col;
        } else {
            frame["line"] = 0;
            frame["column"] = 0;
        }

        stackFrames.push_back(frame);
    }

    // Call stack
    for (int i = vm->callStack.NumObjects(); i >= 1; i--) {
        ScriptCallStack* call = vm->callStack.ObjectAt(i);

        json frame;
        frame["id"] = i;
        frame["name"] = "Called from..."; // We don't easily know the function name of caller without reverse lookup

        // Use codePos to find source
        json source;
        source["path"] = vm->Filename().c_str(); // Assumption: same file.

        frame["source"] = source;

        str sourceLine;
        int col, line;
        if (vm->GetScript()->GetSourceAt(call->codePos, &sourceLine, col, line)) {
            frame["line"] = line;
            frame["column"] = col;
        } else {
            frame["line"] = 0;
            frame["column"] = 0;
        }

        stackFrames.push_back(frame);
    }

    json body;
    body["stackFrames"] = stackFrames;
    body["totalFrames"] = stackFrames.size();
    SendResponse(req, body);
}

void DAPServer::HandleScopes(const json& req) {
    // We expect variables request to follow.
    // We map scopes to variable references.

    // In our simplified model, we only support inspecting the paused thread.
    ScriptVM* pausedVM = nullptr;
    for (auto& pair : m_ThreadStates) {
        if (pair.second.paused) {
            pausedVM = pair.first;
            break;
        }
    }

    if (!pausedVM) {
        SendResponse(req, json::object(), false, "No paused thread");
        return;
    }

    json scopes = json::array();

    {
        json scope;
        scope["name"] = "Locals";
        scope["variablesReference"] = 1; // Locals
        scope["expensive"] = false;
        scopes.push_back(scope);
    }
    {
        json scope;
        scope["name"] = "Globals";
        scope["variablesReference"] = 2; // Globals (ScriptClass)
        scope["expensive"] = false;
        scopes.push_back(scope);
    }
    {
        json scope;
        scope["name"] = "Level";
        scope["variablesReference"] = 3; // Level
        scope["expensive"] = false;
        scopes.push_back(scope);
    }
    {
        json scope;
        scope["name"] = "Game";
        scope["variablesReference"] = 4; // Game
        scope["expensive"] = false;
        scopes.push_back(scope);
    }

    json body;
    body["scopes"] = scopes;
    SendResponse(req, body);
}

int DAPServer::GetVariableReference(ScriptVariable* var) {
    std::lock_guard<std::mutex> lock(m_VarLock);
    m_VariableReferences.push_back(var);
    return m_VariableReferences.size() + 1000; // Offset dynamic refs
}

ScriptVariable* DAPServer::GetVariableFromReference(int ref) {
    std::lock_guard<std::mutex> lock(m_VarLock);
    int idx = ref - 1000 - 1;
    if (idx >= 0 && idx < (int)m_VariableReferences.size()) {
        return m_VariableReferences[idx];
    }
    return nullptr;
}

void DAPServer::HandleVariables(const json& req) {
    int variablesReference = req["arguments"]["variablesReference"];
    json variables = json::array();

    ScriptVM* pausedVM = nullptr;
    for (auto& pair : m_ThreadStates) {
        if (pair.second.paused) {
            pausedVM = pair.first;
            break;
        }
    }

    if (!pausedVM && variablesReference <= 4) {
        SendResponse(req, json::object(), false, "No paused thread");
        return;
    }

    if (variablesReference > 1000) {
        // Variable inspection
        ScriptVariable* var = GetVariableFromReference(variablesReference);
        if (var) {
            if (var->type == VARIABLE_ARRAY && var->m_data.arrayValue) {
                // Iterate array map
                ScriptArrayHolder* holder = var->m_data.arrayValue;
                con_map_enum<ScriptVariable, ScriptVariable> en(holder->arrayValue);
                ScriptVariable* value;

                for (value = en.NextValue(); value != NULL; value = en.NextValue()) {
                    json v;
                    // Key
                    ScriptVariable* key = en.CurrentKey();
                    if (key->type == VARIABLE_STRING || key->type == VARIABLE_CONSTSTRING)
                        v["name"] = key->stringValue().c_str();
                    else if (key->type == VARIABLE_INTEGER)
                        v["name"] = std::to_string(key->intValue());
                    else
                        v["name"] = "key";

                    // Value
                    json ser = SerializeVariable(value);
                    v["value"] = ser["value"];
                    if (ser.contains("variablesReference")) v["variablesReference"] = ser["variablesReference"];
                    else v["variablesReference"] = 0;

                    variables.push_back(v);
                }
            } else if (var->type == VARIABLE_CONSTARRAY && var->m_data.constArrayValue) {
                // Iterate C array
                ScriptConstArrayHolder* holder = var->m_data.constArrayValue;
                for (unsigned int i = 0; i < holder->size; i++) {
                    json v;
                    v["name"] = std::to_string(i);
                    json ser = SerializeVariable(&holder->constArrayValue[i]);
                    v["value"] = ser["value"];
                    if (ser.contains("variablesReference")) v["variablesReference"] = ser["variablesReference"];
                    else v["variablesReference"] = 0;
                    variables.push_back(v);
                }
            }
        }
    } else if (variablesReference == 1 && pausedVM) { // Locals
        ScriptVMStack& stack = pausedVM->m_VMStack;
        ScriptVariable* current = stack.localStack;
        int idx = 0;
        while (current < stack.pTop) {
            json var;
            var["name"] = "local_" + std::to_string(idx++);
            var["value"] = SerializeVariable(current)["value"];
            var["variablesReference"] = 0;
            variables.push_back(var);
            current++;
        }
        json var;
        var["name"] = "Top";
        var["value"] = SerializeVariable(stack.pTop)["value"];
        var["variablesReference"] = 0;
        variables.push_back(var);

    } else if (variablesReference == 2 && pausedVM && pausedVM->m_ScriptClass) { // Globals (Group)
        ScriptVariableList* list = pausedVM->m_ScriptClass->Vars();
        if (list) {
            con_set_enum<short3, ScriptVariable> en(list->list);
            con_set<short3, ScriptVariable>::Entry *entry;
            for (entry = en.NextElement(); entry != NULL; entry = en.NextElement()) {
                json var;
                var["name"] = Director.GetString(entry->GetKey()); // Assuming short3 can be resolved to string if it's an index
                var["value"] = SerializeVariable(&entry->value)["value"];
                var["variablesReference"] = 0;
                variables.push_back(var);
            }
        }
    } else if (variablesReference == 3) { // Level
        ScriptVariableList* list = level.Vars();
        if (list) {
            con_set_enum<short3, ScriptVariable> en(list->list);
            con_set<short3, ScriptVariable>::Entry *entry;
            for (entry = en.NextElement(); entry != NULL; entry = en.NextElement()) {
                json var;
                var["name"] = Director.GetString(entry->GetKey());
                var["value"] = SerializeVariable(&entry->value)["value"];
                var["variablesReference"] = 0;
                variables.push_back(var);
            }
        }
    } else if (variablesReference == 4) { // Game
        ScriptVariableList* list = game.Vars();
        if (list) {
            con_set_enum<short3, ScriptVariable> en(list->list);
            con_set<short3, ScriptVariable>::Entry *entry;
            for (entry = en.NextElement(); entry != NULL; entry = en.NextElement()) {
                json var;
                var["name"] = Director.GetString(entry->GetKey());
                var["value"] = SerializeVariable(&entry->value)["value"];
                var["variablesReference"] = 0;
                variables.push_back(var);
            }
        }
    }

    json body;
    body["variables"] = variables;
    SendResponse(req, body);
}

json DAPServer::SerializeVariable(ScriptVariable* var) {
    json j;
    if (!var) {
        j["value"] = "null";
        return j;
    }

    switch (var->type) {
        case VARIABLE_INTEGER: j["value"] = std::to_string(var->intValue()); break;
        case VARIABLE_FLOAT: j["value"] = std::to_string(var->floatValue()); break;
        case VARIABLE_STRING:
        case VARIABLE_CONSTSTRING:
            j["value"] = "\"" + std::string(var->stringValue().c_str()) + "\"";
            break;
        case VARIABLE_VECTOR: {
            Vector v = var->vectorValue();
            j["value"] = "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
            break;
        }
        case VARIABLE_LISTENER: {
            Listener* l = var->listenerValue();
            if (l) {
                j["value"] = "Listener: " + std::string(l->getClassname());
                if (l->isSubclassOf(Entity)) {
                    Entity* ent = (Entity*)l;
                    j["value"] = "Entity: " + std::string(ent->getClassname()) + " (entnum: " + std::to_string(ent->entnum) + ")";
                }
            } else {
                j["value"] = "Listener: (null)";
            }
            break;
        }
        case VARIABLE_ARRAY: {
            j["value"] = "Array[" + std::to_string(var->arraysize()) + "]";
            j["variablesReference"] = GetVariableReference(var);
            break;
        }
        case VARIABLE_CONSTARRAY: {
            j["value"] = "ConstArray[" + std::to_string(var->arraysize()) + "]";
            j["variablesReference"] = GetVariableReference(var);
            break;
        }
        default: j["value"] = var->GetTypeName(); break;
    }
    return j;
}

void DAPServer::HandleContinue(const json& req) {
    {
        std::lock_guard<std::mutex> lock(m_VarLock);
        m_VariableReferences.clear();
    }

    long long threadId = req["arguments"]["threadId"];
    ScriptVM* vm = GetVM((int)threadId);

    if (vm) {
        m_ThreadStates[vm].paused = false;
        m_ThreadStates[vm].stepMode = STEP_NONE;
        m_ThreadStates[vm].ignoreBreakpointAddr = vm->m_CodePos;
        vm->state = STATE_RUNNING;
    }

    SendResponse(req);
}

void DAPServer::HandlePause(const json& req) {
    long long threadId = req["arguments"]["threadId"];
    ScriptVM* vm = GetVM((int)threadId);

    if (vm) {
        m_ThreadStates[vm].paused = true;
        m_ThreadStates[vm].pauseReason = "pause";

        json body;
        body["reason"] = "pause";
        body["threadId"] = (int)threadId;
        SendEvent("stopped", body);
    }
    SendResponse(req);
}

void DAPServer::HandleNext(const json& req) {
    {
        std::lock_guard<std::mutex> lock(m_VarLock);
        m_VariableReferences.clear();
    }

    long long threadId = req["arguments"]["threadId"];
    ScriptVM* vm = GetVM((int)threadId);

    if (vm) {
        m_ThreadStates[vm].paused = false;
        m_ThreadStates[vm].stepMode = STEP_OVER;
        m_ThreadStates[vm].stepDepth = vm->callStack.NumObjects();
        m_ThreadStates[vm].ignoreBreakpointAddr = vm->m_CodePos;

        // Capture current line for line-stepping
        str sourceLine;
        int col, line;
        vm->GetScript()->GetSourceAt(vm->m_CodePos, &sourceLine, col, line);
        m_ThreadStates[vm].startLine = line;

        vm->state = STATE_RUNNING;
    }

    SendResponse(req);
}

void DAPServer::HandleStepIn(const json& req) {
    {
        std::lock_guard<std::mutex> lock(m_VarLock);
        m_VariableReferences.clear();
    }

    long long threadId = req["arguments"]["threadId"];
    ScriptVM* vm = GetVM((int)threadId);

    if (vm) {
        m_ThreadStates[vm].paused = false;
        m_ThreadStates[vm].stepMode = STEP_IN;
        m_ThreadStates[vm].ignoreBreakpointAddr = vm->m_CodePos;

        str sourceLine;
        int col, line;
        vm->GetScript()->GetSourceAt(vm->m_CodePos, &sourceLine, col, line);
        m_ThreadStates[vm].startLine = line;

        vm->state = STATE_RUNNING;
    }

    SendResponse(req);
}

void DAPServer::HandleStepOut(const json& req) {
    {
        std::lock_guard<std::mutex> lock(m_VarLock);
        m_VariableReferences.clear();
    }

    long long threadId = req["arguments"]["threadId"];
    ScriptVM* vm = GetVM((int)threadId);

    if (vm) {
        m_ThreadStates[vm].paused = false;
        m_ThreadStates[vm].stepMode = STEP_OUT;
        m_ThreadStates[vm].stepDepth = vm->callStack.NumObjects();
        m_ThreadStates[vm].ignoreBreakpointAddr = vm->m_CodePos;
        vm->state = STATE_RUNNING;
    }

    SendResponse(req);
}

bool DAPServer::CheckDebugHook(ScriptVM* vm) {
    if (!m_DebuggerAttached.load(std::memory_order_relaxed)) return false;

    // Register VM if not known (lazy registration)
    RegisterVM(vm);

    // Check for paused state
    if (m_ThreadStates[vm].paused) {
        vm->state = STATE_DEBUG_WAIT;
        return true;
    }

    // Check breakpoints
    auto it = m_ActiveBreakpoints.find(vm->m_CodePos);
    if (it != m_ActiveBreakpoints.end()) {
        if (m_ThreadStates[vm].ignoreBreakpointAddr == vm->m_CodePos) {
            // We just resumed from this breakpoint, so ignore it once
            m_ThreadStates[vm].ignoreBreakpointAddr = nullptr;
        } else {
            m_ThreadStates[vm].paused = true;
            m_ThreadStates[vm].pauseReason = "breakpoint";
            m_ThreadStates[vm].stepMode = STEP_NONE;

            json body;
            body["reason"] = "breakpoint";
            body["threadId"] = m_VMToID[vm];
            SendEvent("stopped", body);
        vm->state = STATE_DEBUG_WAIT;
            return true;
        }
    } else {
        // Clear ignore addr if we moved off it
        m_ThreadStates[vm].ignoreBreakpointAddr = nullptr;
    }

    // Check stepping
    StepMode mode = m_ThreadStates[vm].stepMode;
    if (mode != STEP_NONE) {
        int currentDepth = vm->callStack.NumObjects();
        bool stop = false;

        // Get current line
        str sourceLine;
        int col, currentLine;
        vm->GetScript()->GetSourceAt(vm->m_CodePos, &sourceLine, col, currentLine);

        // Only stop if line changed (except StepOut)
        bool lineChanged = (currentLine != m_ThreadStates[vm].startLine);

        if (mode == STEP_IN) {
            if (lineChanged) stop = true;
        } else if (mode == STEP_OVER) {
            // Stop if depth is same or less AND line changed
            if (currentDepth <= m_ThreadStates[vm].stepDepth && lineChanged) stop = true;
        } else if (mode == STEP_OUT) {
            // Stop if depth is less
            if (currentDepth < m_ThreadStates[vm].stepDepth) stop = true;
        }

        if (stop) {
            m_ThreadStates[vm].paused = true;
            m_ThreadStates[vm].pauseReason = "step";
            m_ThreadStates[vm].stepMode = STEP_NONE;

            json body;
            body["reason"] = "step";
            body["threadId"] = m_VMToID[vm];
            SendEvent("stopped", body);
            vm->state = STATE_DEBUG_WAIT;
            return true;
        }
    }

    return false;
}

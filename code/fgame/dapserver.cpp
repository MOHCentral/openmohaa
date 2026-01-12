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

#include <algorithm>
#include <cctype>
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
#include <sys/stat.h>
#include <dirent.h>

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

// Helper function to resolve script path case-insensitively
static std::string ResolveScriptPath(const std::string& scriptPath) {
    // Build the full base path
    const char* homedir = getenv("HOME");
    if (!homedir) {
        homedir = "/home/elgan";  // fallback
    }
    
    std::string basePath = std::string(homedir) + "/.local/share/openmohaa/main/";
    std::string fullPath = basePath + scriptPath;
    
    // Quick check if exact path exists
    struct stat statbuf;
    if (stat(fullPath.c_str(), &statbuf) == 0) {
        return fullPath;
    }
    
    // Case-insensitive path resolution
    std::string result = basePath;
    std::istringstream pathStream(scriptPath);
    std::string component;
    
    while (std::getline(pathStream, component, '/')) {
        if (component.empty()) continue;
        
        // Try to find this component case-insensitively
        DIR* dir = opendir(result.c_str());
        if (!dir) {
            // Directory doesn't exist, return best guess
            return fullPath;
        }
        
        bool found = false;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // Skip . and ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            // Case-insensitive comparison
            std::string entryName = entry->d_name;
            std::string lowerEntry = entryName;
            std::string lowerComponent = component;
            std::transform(lowerEntry.begin(), lowerEntry.end(), lowerEntry.begin(), ::tolower);
            std::transform(lowerComponent.begin(), lowerComponent.end(), lowerComponent.begin(), ::tolower);
            
            if (lowerEntry == lowerComponent) {
                // Found it with the correct case
                result += entryName + "/";
                found = true;
                break;
            }
        }
        closedir(dir);
        
        if (!found) {
            // Component not found, return best guess
            return fullPath;
        }
    }
    
    // Remove trailing slash
    if (!result.empty() && result[result.length() - 1] == '/') {
        result = result.substr(0, result.length() - 1);
    }
    
    return result;
}

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
    if (m_Running) {
        gi.Printf("DAP: Server already running, skipping Start()\n");
        return;
    }

    gi.Printf("====================================\n");
    gi.Printf("DAP: Starting Debug Adapter Protocol server on port %d\n", port);
    gi.Printf("====================================\n");

    m_Running = true;
    m_NetworkThread = std::thread([this, port]() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        m_ServerSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_ServerSocket == INVALID_SOCKET) {
            gi.Printf("DAP: ERROR - Failed to create socket\n");
            return;
        }

        int opt = 1;
        setsockopt(m_ServerSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(m_ServerSocket, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
            gi.Printf("DAP: ERROR - Failed to bind to port %d\n", port);
            closesocket(m_ServerSocket);
            return;
        }

        if (listen(m_ServerSocket, 1) == SOCKET_ERROR) {
            gi.Printf("DAP: ERROR - Failed to listen\n");
            closesocket(m_ServerSocket);
            return;
        }

        gi.Printf("DAP: Server listening on port %d - Ready for VSCode connection\n", port);

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

bool DAPServer::IsPaused(ScriptVM* vm) {
    if (!vm) return false;
    auto it = m_ThreadStates.find(vm);
    if (it == m_ThreadStates.end()) return false;
    return it->second.paused;
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

        gi.Printf("DAP: ✓ VSCode debugger connected!\n");
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
                    gi.Printf("DAP: VSCode debugger disconnected - resuming all paused scripts\n");
                    closesocket(m_ClientSocket);
                    m_ClientSocket = INVALID_SOCKET;
                    m_DebuggerAttached = false;
                    
                    // Resume all paused VMs when debugger disconnects
                    for (auto& pair : m_ThreadStates) {
                        ScriptVM* vm = pair.first;
                        if (pair.second.paused && vm) {
                            gi.Printf("DAP:   Resuming paused VM for script '%s'\n", vm->Filename().c_str());
                            vm->state = STATE_RUNNING;
                            pair.second.paused = false;
                            pair.second.stepMode = STEP_NONE;
                        }
                    }
                    
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
                        // gi.Printf("DAP: JSON Parse Error: %s\n", e.what());
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
    else if (command == "evaluate") HandleEvaluate(msg);
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
    body["supportsEvaluateForHovers"] = true;
    body["exceptionBreakpointFilters"] = json::array();

    SendResponse(req, body);

    SendEvent("initialized");
}

void DAPServer::HandleLaunch(const json& req) {
    gi.Printf("DAP: ★★★ DEBUGGER LAUNCHING - m_DebuggerAttached = true ★★★\n");
    m_DebuggerAttached = true;
    SendResponse(req);
}

void DAPServer::HandleAttach(const json& req) {
    gi.Printf("DAP: ★★★ DEBUGGER ATTACHING - m_DebuggerAttached = true ★★★\n");
    m_DebuggerAttached = true;
    SendResponse(req);
}

void DAPServer::HandleConfigurationDone(const json& req) {
    UpdateBreakpoints();
    SendResponse(req);
}

void DAPServer::HandleSetBreakpoints(const json& req) {
    std::string file = req["arguments"]["source"]["path"];

    // Normalize file path
    std::replace(file.begin(), file.end(), '\\', '/');
    std::transform(file.begin(), file.end(), file.begin(), ::tolower);

    gi.Printf("DAP: SetBreakpoints request for file: '%s'\n", file.c_str());

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

            gi.Printf("DAP: Breakpoint requested at '%s:%d'\n", file.c_str(), line);
        }
    }

    // Try to verify breakpoints immediately
    UpdateBreakpoints();

    // Build response with verification status
    json responseBody;
    json actualBreakpoints = json::array();
    for (const auto& bp : m_RequestedBreakpoints[file]) {
        json resBp;
        resBp["verified"] = bp.verified;
        resBp["line"] = bp.line;
        if (bp.verified) {
            gi.Printf("DAP: Breakpoint VERIFIED at '%s:%d'\n", file.c_str(), bp.line);
        } else {
            gi.Printf("DAP: Breakpoint UNVERIFIED at '%s:%d' (script may not be loaded yet)\n", file.c_str(), bp.line);
        }
        actualBreakpoints.push_back(resBp);
    }
    responseBody["breakpoints"] = actualBreakpoints;

    SendResponse(req, responseBody);
}

void DAPServer::UpdateBreakpoints() {
    m_ActiveBreakpoints.clear();

    gi.Printf("DAP: UpdateBreakpoints called. Checking %zu requested breakpoint files\n", 
               m_RequestedBreakpoints.size());

    // Iterate all loaded scripts and try to match breakpoints
    // This is expensive so we do it only when breakpoints change or scripts load
    // For now we do it when requested.

    con_map_enum<const_str, GameScript *> en(Director.m_GameScripts);
    GameScript **g;

    int scriptCount = 0;
    for (g = en.NextValue(); g != NULL; g = en.NextValue()) {
        scriptCount++;
        GameScript* script = *g;
        if (!script) {
            gi.Printf("DAP: Skipping NULL script at index %d\n", scriptCount);
            continue;
        }
        gi.Printf("DAP: Checking script %d: '%s'\n", scriptCount, script->Filename().c_str());
        MapBreakpointsForScript(script);
    }
    
    gi.Printf("DAP: UpdateBreakpoints complete. Found %zu active breakpoint(s)\n", 
               m_ActiveBreakpoints.size());
}

void DAPServer::MapBreakpointsForScript(GameScript* script) {
    if (!script) return;

    str filename = script->Filename();
    std::string scriptFile = filename.c_str();

    // Normalize scriptFile path: convert backslashes to forward slashes
    std::replace(scriptFile.begin(), scriptFile.end(), '\\', '/');
    std::transform(scriptFile.begin(), scriptFile.end(), scriptFile.begin(), ::tolower);

    // Find matching requested breakpoints
    // Try to match by file extension and filename patterns
    std::vector<BreakpointInfo>* requested = nullptr;

    for (auto& pair : m_RequestedBreakpoints) {
        std::string reqFile = pair.first;
        std::replace(reqFile.begin(), reqFile.end(), '\\', '/');
        std::transform(reqFile.begin(), reqFile.end(), reqFile.begin(), ::tolower);

        // Improved matching:
        // 1. If reqFile ends with scriptFile, it's a match (e.g., "/full/path/maps/test.scr" matches "maps/test.scr")
        // 2. If they are identical
        // 3. If reqFile contains scriptFile as a path component (better than simple substring)
        
        bool matches = false;
        
        // Exact match
        if (reqFile == scriptFile) {
            matches = true;
        }
        // reqFile ends with scriptFile (full path matches relative path)
        else if (reqFile.length() > scriptFile.length() && 
                 reqFile.substr(reqFile.length() - scriptFile.length()) == scriptFile &&
                 (scriptFile[0] != '/' && (reqFile[reqFile.length() - scriptFile.length() - 1] == '/' || 
                                           reqFile[reqFile.length() - scriptFile.length() - 1] == '\\'))) {
            matches = true;
        }
        // scriptFile ends with reqFile (relative matches relative)
        else if (scriptFile.length() > reqFile.length() &&
                 scriptFile.substr(scriptFile.length() - reqFile.length()) == reqFile &&
                 (reqFile[0] != '/' && (scriptFile[scriptFile.length() - reqFile.length() - 1] == '/' ||
                                        scriptFile[scriptFile.length() - reqFile.length() - 1] == '\\'))) {
            matches = true;
        }
        // Check if both filenames match (after "main/" or similar prefixes)
        else {
            size_t reqSlash = reqFile.rfind('/');
            size_t scriptSlash = scriptFile.rfind('/');
            if (reqSlash != std::string::npos && scriptSlash != std::string::npos) {
                std::string reqName = reqFile.substr(reqSlash);
                std::string scriptName = scriptFile.substr(scriptSlash);
                if (reqName == scriptName) {
                    matches = true;
                }
            }
        }

        if (matches) {
            requested = &pair.second;
            gi.Printf("DAP: Breakpoint file match found: Requested='%s' Script='%s'\n", reqFile.c_str(), scriptFile.c_str());
            break;
        }
    }

    if (!requested) {
        gi.Printf("DAP: No breakpoint match for script: '%s'\n", scriptFile.c_str());
        return;
    }

    // Verify that the script has a valid source map
    if (!script->m_ProgToSource) {
        gi.Printf("DAP: Script '%s' has no source map (m_ProgToSource is null)\n", scriptFile.c_str());
        return;
    }

    gi.Printf("DAP: Script '%s' HAS source map, iterating entries...\n", scriptFile.c_str());
    
    con_set_enum<const uchar*, sourceinfo_t> en(*script->m_ProgToSource);
    con_set<const uchar *, sourceinfo_t>::Entry *entry;
    
    int sourceMapEntries = 0;
    int matched = 0;
    for (entry = en.NextElement(); entry != NULL; entry = en.NextElement()) {
        sourceMapEntries++;
        int line = entry->value.line;
        unsigned char* code = (unsigned char*)entry->GetKey();

        gi.Printf("DAP:   SourceMap entry %d: line=%d, codePtr=%p\n", sourceMapEntries, line, code);

        for (auto& bp : *requested) {
            if (bp.line == line) {
                m_ActiveBreakpoints[code] = bp.id;
                bp.verified = true;
                matched++;
                gi.Printf("DAP:     ✓ MATCHED breakpoint! Added to m_ActiveBreakpoints[%p]\n", code);
            }
        }
    }
    
    gi.Printf("DAP: Script '%s' source map had %d entries, matched %d breakpoint(s)\n", 
               scriptFile.c_str(), sourceMapEntries, matched);
    
    // Log unverified breakpoints for this file
    for (const auto& bp : *requested) {
        if (!bp.verified) {
            gi.Printf("DAP: ✗ Could not verify breakpoint at '%s:%d' - line not in source map\n", 
                      scriptFile.c_str(), bp.line);
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

    // Get the script filename and resolve to full path with case-insensitive lookup
    std::string scriptPath = vm->Filename().c_str();
    std::string fullPath = ResolveScriptPath(scriptPath);
    
    // Extract the workspace-relative path (strip the base directory)
    // The resolved path is like: /home/user/.local/share/openmohaa/main/maps/DM/mohdm1.scr
    // We want to extract: maps/DM/mohdm1.scr (with corrected case)
    const char* homedir = getenv("HOME");
    if (!homedir) homedir = "/home/elgan";
    std::string basePath = std::string(homedir) + "/.local/share/openmohaa/main/";
    
    std::string workspaceRelativePath = fullPath;
    if (fullPath.find(basePath) == 0) {
        workspaceRelativePath = fullPath.substr(basePath.length());
    }
    
    gi.Printf("DAP: Resolved script path: '%s' -> '%s' (workspace-relative: '%s')\n", 
              scriptPath.c_str(), fullPath.c_str(), workspaceRelativePath.c_str());

    // Current frame
    {
        json frame;
        frame["id"] = 0;
        frame["name"] = vm->Label().c_str();

        json source;
        source["path"] = workspaceRelativePath;  // Send workspace-relative path with corrected case
        source["name"] = scriptPath; // Short name for display
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
        frame["name"] = "Called from...";

        // Use codePos to find source
        json source;
        source["path"] = workspaceRelativePath;
        source["name"] = scriptPath;

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

    // Get frameId from request
    int frameId = 0;
    if (req["arguments"].contains("frameId")) {
        frameId = req["arguments"]["frameId"];
    }

    json scopes = json::array();

    {
        json scope;
        scope["name"] = "Locals";
        // Encode frameId into the variablesReference: 1000 + frameId
        scope["variablesReference"] = 1000 + frameId; 
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
    return m_VariableReferences.size() + 2000; // Offset dynamic refs (2000+ for arrays/objects)
}

ScriptVariable* DAPServer::GetVariableFromReference(int ref) {
    std::lock_guard<std::mutex> lock(m_VarLock);
    int idx = ref - 2000 - 1;
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

    if (!pausedVM && variablesReference <= 2000) {
        SendResponse(req, json::object(), false, "No paused thread");
        return;
    }

    // Handle local variables (1000-1999 range encodes frameId)
    if (variablesReference >= 1000 && variablesReference < 2000) {
        int frameId = variablesReference - 1000;
        
        ScriptVariable* localStart = nullptr;
        ScriptVariable* localEnd = nullptr;
        
        if (frameId == 0) {
            // Current frame - use current stack
            localStart = pausedVM->m_VMStack.localStack;
            localEnd = pausedVM->m_VMStack.pTop;
        } else {
            // Previous frame - use call stack
            int callStackIdx = pausedVM->callStack.NumObjects() - frameId + 1;
            if (callStackIdx >= 1 && callStackIdx <= pausedVM->callStack.NumObjects()) {
                ScriptCallStack* call = pausedVM->callStack.ObjectAt(callStackIdx);
                localStart = call->localStack;
                localEnd = call->pTop;
            }
        }
        
        if (localStart && localEnd) {
            int idx = 0;
            ScriptVariable* current = localStart;
            GameScript* script = pausedVM->GetScript();
            unsigned char* codePos = (frameId == 0) ? pausedVM->m_CodePos : 
                                      pausedVM->callStack.ObjectAt(pausedVM->callStack.NumObjects() - frameId + 1)->codePos;
            
            while (current < localEnd) {
                json var;
                
                // Try to get variable name from debug symbol table
                const char* varName = nullptr;
                if (script && codePos) {
                    varName = script->GetLocalVarName(codePos, idx);
                }
                
                if (varName) {
                    var["name"] = varName;
                } else {
                    var["name"] = "local_" + std::to_string(idx);
                }
                
                // Get serialized variable with type info
                json ser = SerializeVariable(current);
                var["value"] = ser["value"];
                
                // Add type information
                std::string typeStr = "";
                switch (current->type) {
                    case VARIABLE_NONE: typeStr = "none"; break;
                    case VARIABLE_STRING: typeStr = "string"; break;
                    case VARIABLE_CONSTSTRING: typeStr = "conststring"; break;
                    case VARIABLE_INTEGER: typeStr = "int"; break;
                    case VARIABLE_FLOAT: typeStr = "float"; break;
                    case VARIABLE_VECTOR: typeStr = "vector"; break;
                    case VARIABLE_ARRAY: typeStr = "array"; break;
                    case VARIABLE_CONSTARRAY: typeStr = "constarray"; break;
                    case VARIABLE_LISTENER: typeStr = "listener"; break;
                    default: typeStr = "type_" + std::to_string(current->type); break;
                }
                var["type"] = typeStr;
                
                if (ser.contains("variablesReference")) {
                    var["variablesReference"] = ser["variablesReference"];
                } else {
                    var["variablesReference"] = 0;
                }
                
                variables.push_back(var);
                current++;
                idx++;
            }
        }
    } else if (variablesReference > 2000) {
        // Variable inspection (arrays, etc.)
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
    } else if (variablesReference == 2 && pausedVM && pausedVM->m_ScriptClass) { // Globals (Group)
        ScriptVariableList* list = pausedVM->m_ScriptClass->Vars();
        if (list) {
            con_set_enum<short3, ScriptVariable> en(list->list);
            con_set<short3, ScriptVariable>::Entry *entry;
            for (entry = en.NextElement(); entry != NULL; entry = en.NextElement()) {
                json var;
                var["name"] = Director.GetString(entry->GetKey());
                json ser = SerializeVariable(&entry->value);
                var["value"] = ser["value"];
                
                // Add type information
                std::string typeStr = "";
                switch (entry->value.type) {
                    case VARIABLE_NONE: typeStr = "none"; break;
                    case VARIABLE_STRING: typeStr = "string"; break;
                    case VARIABLE_CONSTSTRING: typeStr = "conststring"; break;
                    case VARIABLE_INTEGER: typeStr = "int"; break;
                    case VARIABLE_FLOAT: typeStr = "float"; break;
                    case VARIABLE_VECTOR: typeStr = "vector"; break;
                    case VARIABLE_ARRAY: typeStr = "array"; break;
                    case VARIABLE_CONSTARRAY: typeStr = "constarray"; break;
                    case VARIABLE_LISTENER: typeStr = "listener"; break;
                    default: typeStr = "type_" + std::to_string(entry->value.type); break;
                }
                var["type"] = typeStr;
                
                if (ser.contains("variablesReference")) {
                    var["variablesReference"] = ser["variablesReference"];
                } else {
                    var["variablesReference"] = 0;
                }
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
                json ser = SerializeVariable(&entry->value);
                var["value"] = ser["value"];
                
                // Add type information
                std::string typeStr = "";
                switch (entry->value.type) {
                    case VARIABLE_NONE: typeStr = "none"; break;
                    case VARIABLE_STRING: typeStr = "string"; break;
                    case VARIABLE_CONSTSTRING: typeStr = "conststring"; break;
                    case VARIABLE_INTEGER: typeStr = "int"; break;
                    case VARIABLE_FLOAT: typeStr = "float"; break;
                    case VARIABLE_VECTOR: typeStr = "vector"; break;
                    case VARIABLE_ARRAY: typeStr = "array"; break;
                    case VARIABLE_CONSTARRAY: typeStr = "constarray"; break;
                    case VARIABLE_LISTENER: typeStr = "listener"; break;
                    default: typeStr = "type_" + std::to_string(entry->value.type); break;
                }
                var["type"] = typeStr;
                
                if (ser.contains("variablesReference")) {
                    var["variablesReference"] = ser["variablesReference"];
                } else {
                    var["variablesReference"] = 0;
                }
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
                json ser = SerializeVariable(&entry->value);
                var["value"] = ser["value"];
                
                // Add type information
                std::string typeStr = "";
                switch (entry->value.type) {
                    case VARIABLE_NONE: typeStr = "none"; break;
                    case VARIABLE_STRING: typeStr = "string"; break;
                    case VARIABLE_CONSTSTRING: typeStr = "conststring"; break;
                    case VARIABLE_INTEGER: typeStr = "int"; break;
                    case VARIABLE_FLOAT: typeStr = "float"; break;
                    case VARIABLE_VECTOR: typeStr = "vector"; break;
                    case VARIABLE_ARRAY: typeStr = "array"; break;
                    case VARIABLE_CONSTARRAY: typeStr = "constarray"; break;
                    case VARIABLE_LISTENER: typeStr = "listener"; break;
                    default: typeStr = "type_" + std::to_string(entry->value.type); break;
                }
                var["type"] = typeStr;
                
                if (ser.contains("variablesReference")) {
                    var["variablesReference"] = ser["variablesReference"];
                } else {
                    var["variablesReference"] = 0;
                }
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
        gi.Printf("DAP: HandleContinue - Setting paused=false and state=STATE_RUNNING for VM %p\n", vm);
        m_ThreadStates[vm].paused = false;
        m_ThreadStates[vm].stepMode = STEP_NONE;
        m_ThreadStates[vm].ignoreBreakpointAddr = vm->m_CodePos;
        vm->state = STATE_RUNNING;
        gi.Printf("DAP: HandleContinue - DONE. VM state is now %d (should be %d for STATE_RUNNING)\n", vm->state, STATE_RUNNING);
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
    static int callCount = 0;
    callCount++;
    
    // Log first 5 calls to confirm this function is being called
    if (callCount <= 5) {
        gi.Printf("DAP: CheckDebugHook called #%d - codePos=%p, attached=%d, activeBreakpoints=%lu\n", 
                  callCount, vm->m_CodePos, m_DebuggerAttached.load(std::memory_order_relaxed), 
                  m_ActiveBreakpoints.size());
    }
    
    if (!m_DebuggerAttached.load(std::memory_order_relaxed)) return false;

    // Register VM if not known (lazy registration)
    RegisterVM(vm);

    // Check for paused state
    if (m_ThreadStates[vm].paused) {
        gi.Printf("DAP: CheckDebugHook - VM %p is paused, setting state to STATE_DEBUG_WAIT\n", vm);
        vm->state = STATE_DEBUG_WAIT;
        return true;
    }

    // Check breakpoints - log if we're near a breakpoint
    auto it = m_ActiveBreakpoints.find(vm->m_CodePos);
    if (it != m_ActiveBreakpoints.end()) {
        gi.Printf("DAP: ★★★ BREAKPOINT HIT at %p (breakpoint id: %d) ★★★\n", vm->m_CodePos, it->second);
        if (m_ThreadStates[vm].ignoreBreakpointAddr == vm->m_CodePos) {
            // We just resumed from this breakpoint, so ignore it once
            m_ThreadStates[vm].ignoreBreakpointAddr = nullptr;
            gi.Printf("DAP:   But ignoring (just resumed from here)\n");
        } else {
            m_ThreadStates[vm].paused = true;
            m_ThreadStates[vm].pauseReason = "breakpoint";
            m_ThreadStates[vm].stepMode = STEP_NONE;

            json body;
            body["reason"] = "breakpoint";
            body["threadId"] = m_VMToID[vm];
            SendEvent("stopped", body);
        vm->state = STATE_DEBUG_WAIT;
            gi.Printf("DAP:   VM paused. Notified client.\n");
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

        static int stepCount = 0;
        stepCount++;
        if (stepCount <= 10) {
            gi.Printf("DAP: STEP CHECK #%d - mode=%d, currentLine=%d, startLine=%d, lineChanged=%d, currentDepth=%d, stepDepth=%d\n",
                      stepCount, mode, currentLine, m_ThreadStates[vm].startLine, lineChanged, currentDepth, m_ThreadStates[vm].stepDepth);
        }

        if (mode == STEP_IN) {
            if (lineChanged) stop = true;
        } else if (mode == STEP_OVER) {
            // Stop if depth is same or less AND line changed
            if (currentDepth <= m_ThreadStates[vm].stepDepth && lineChanged) {
                gi.Printf("DAP: STEP OVER should stop - currentDepth=%d, stepDepth=%d, lineChanged=%d\n",
                          currentDepth, m_ThreadStates[vm].stepDepth, lineChanged);
                stop = true;
            }
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

void DAPServer::OnScriptLoaded(GameScript* script) {
    if (!m_DebuggerAttached.load(std::memory_order_relaxed)) return;
    if (!script) return;

    // Try to verify breakpoints for this newly loaded script
    gi.Printf("DAP: Script loaded: '%s', attempting to verify breakpoints...\n", script->Filename().c_str());
    
    MapBreakpointsForScript(script);
}

void DAPServer::HandleEvaluate(const json& req) {
    std::string expression = req["arguments"]["expression"];
    
    // Optional frameId for context
    int frameId = 0;
    if (req["arguments"].contains("frameId")) {
        frameId = req["arguments"]["frameId"];
    }
    
    // Check the context - if it's "hover", we want to evaluate for hover tooltip
    std::string context = "";
    if (req["arguments"].contains("context")) {
        context = req["arguments"]["context"];
    }

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

    ScriptVariable* foundVar = nullptr;
    bool found = false;
    GameScript* script = pausedVM->GetScript();
    
    // Try to find local variable by name first (if we have debug symbols)
    if (!found && script) {
        ScriptVariable* localStart = nullptr;
        ScriptVariable* localEnd = nullptr;
        unsigned char* codePos = nullptr;
        
        if (frameId == 0) {
            localStart = pausedVM->m_VMStack.localStack;
            localEnd = pausedVM->m_VMStack.pTop;
            codePos = pausedVM->m_CodePos;
        } else {
            int callStackIdx = pausedVM->callStack.NumObjects() - frameId + 1;
            if (callStackIdx >= 1 && callStackIdx <= pausedVM->callStack.NumObjects()) {
                ScriptCallStack* call = pausedVM->callStack.ObjectAt(callStackIdx);
                localStart = call->localStack;
                localEnd = call->pTop;
                codePos = call->codePos;
            }
        }
        
        if (localStart && localEnd && codePos) {
            int idx = 0;
            ScriptVariable* current = localStart;
            while (current < localEnd) {
                const char* varName = script->GetLocalVarName(codePos, idx);
                if (varName && expression == varName) {
                    foundVar = current;
                    found = true;
                    break;
                }
                current++;
                idx++;
            }
        }
    }
    
    // Try local_N syntax for unnamed variables
    if (!found && expression.rfind("local_", 0) == 0) {
        try {
            int idx = std::stoi(expression.substr(6));
            
            ScriptVariable* localStart = nullptr;
            ScriptVariable* localEnd = nullptr;
            
            if (frameId == 0) {
                localStart = pausedVM->m_VMStack.localStack;
                localEnd = pausedVM->m_VMStack.pTop;
            } else {
                int callStackIdx = pausedVM->callStack.NumObjects() - frameId + 1;
                if (callStackIdx >= 1 && callStackIdx <= pausedVM->callStack.NumObjects()) {
                    ScriptCallStack* call = pausedVM->callStack.ObjectAt(callStackIdx);
                    localStart = call->localStack;
                    localEnd = call->pTop;
                }
            }
            
            if (localStart && localStart + idx < localEnd) {
                foundVar = localStart + idx;
                found = true;
            }
        } catch (...) {}
    }

    // Search Globals (ScriptClass)
    if (!found && pausedVM->m_ScriptClass) {
        ScriptVariableList* list = pausedVM->m_ScriptClass->Vars();
        if (list) {
            // list is con_set<short3, ScriptVariable>
            // We need to resolve 'expression' string to short3 or find entry by string match.
            // Using Director.GetString to reverse map short3 is slow if we have to iterate all.
            // Instead, we can try to look up key index if StringDict supports it.
            // Assuming StringDict.findKeyIndex(expression) works.
            
            // Note: StringDict.findKeyIndex returns 0 if not found, usually.
            // Let's iterate linearly for now as it's safer without deep knowledge of string system details.
            con_set_enum<short3, ScriptVariable> en(list->list);
            con_set<short3, ScriptVariable>::Entry *entry;
            for (entry = en.NextElement(); entry != NULL; entry = en.NextElement()) {
                const char* name = Director.GetString(entry->GetKey());
                if (name && expression == name) {
                    foundVar = &entry->value;
                    found = true;
                    break;
                }
            }
        }
    }

    // 3. Level
    if (!found) {
        ScriptVariableList* list = level.Vars();
        if (list) {
             con_set_enum<short3, ScriptVariable> en(list->list);
            con_set<short3, ScriptVariable>::Entry *entry;
            for (entry = en.NextElement(); entry != NULL; entry = en.NextElement()) {
                 const char* name = Director.GetString(entry->GetKey());
                 if (name && expression == name) {
                     foundVar = &entry->value;
                     found = true;
                     break;
                 }
            }
        }
    }

    // 4. Game
    if (!found) {
        ScriptVariableList* list = game.Vars();
        if (list) {
             con_set_enum<short3, ScriptVariable> en(list->list);
             con_set<short3, ScriptVariable>::Entry *entry;
            for (entry = en.NextElement(); entry != NULL; entry = en.NextElement()) {
                 const char* name = Director.GetString(entry->GetKey());
                 if (name && expression == name) {
                     foundVar = &entry->value;
                     found = true;
                     break;
                 }
            }
        }
    }
    
    // 5. Special keyword 'local'
    // If we want to inspect local stack by index? e.g. "local_0"
    if (!found && expression.rfind("local_", 0) == 0) {
        try {
            int idx = std::stoi(expression.substr(6));
             ScriptVMStack& stack = pausedVM->m_VMStack;
             if (stack.localStack + idx < stack.pTop) {
                 foundVar = stack.localStack + idx;
                 found = true;
             }
        } catch (...) {}
    }

    json body;
    if (found && foundVar) {
        json ser = SerializeVariable(foundVar);
        body["result"] = ser["value"];
        if (ser.contains("variablesReference")) {
            body["variablesReference"] = ser["variablesReference"];
        } else {
             body["variablesReference"] = 0;
        }
    } else {
        // Not found
        body["result"] = "undefined";
        body["variablesReference"] = 0;
    }

    SendResponse(req, body);
}

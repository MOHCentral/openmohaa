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

#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

#include "../qcommon/json.hpp"
using json = nlohmann::json;

class ScriptVM;
class ScriptVariable;
class GameScript;

// Forward declaration for platform specific socket
#ifdef _WIN32
    #include <winsock2.h>
    typedef SOCKET socket_t;
#else
    typedef int socket_t;
#endif

class DAPServer {
public:
    static DAPServer& Get();

    DAPServer();
    ~DAPServer();

    void Start(int port);
    void Stop();
    void RunFrame();

    // Hook called from ScriptVM::Execute
    // Returns true if the VM should pause execution (yield)
    bool CheckDebugHook(ScriptVM* vm);

    bool IsActive() const { return m_DebuggerAttached.load(std::memory_order_relaxed); }

    // Registry management
    int RegisterVM(ScriptVM* vm);
    void UnregisterVM(ScriptVM* vm);
    ScriptVM* GetVM(int id);

private:
    void NetworkLoop();
    void ProcessMessage(const json& msg);
    void SendResponse(const json& req, const json& body = json::object(), bool success = true, const std::string& message = "");
    void SendEvent(const std::string& event, const json& body = json::object());

    // DAP Request Handlers
    void HandleInitialize(const json& req);
    void HandleLaunch(const json& req);
    void HandleAttach(const json& req);
    void HandleSetBreakpoints(const json& req);
    void HandleConfigurationDone(const json& req);
    void HandleThreads(const json& req);
    void HandleStackTrace(const json& req);
    void HandleScopes(const json& req);
    void HandleVariables(const json& req);
    void HandleContinue(const json& req);
    void HandleNext(const json& req);
    void HandleStepIn(const json& req);
    void HandleStepOut(const json& req);
    void HandlePause(const json& req);

    // Helpers
    void UpdateBreakpoints();
    void MapBreakpointsForScript(GameScript* script);
    json SerializeVariable(ScriptVariable* var);
    int GetVariableReference(ScriptVariable* var);
    ScriptVariable* GetVariableFromReference(int ref);

private:
    std::atomic<bool> m_Running;
    std::atomic<bool> m_DebuggerAttached;
    std::thread m_NetworkThread;
    socket_t m_ServerSocket;
    socket_t m_ClientSocket;

    // Message queues
    std::mutex m_QueueLock;
    std::queue<json> m_IncomingQueue;
    std::queue<std::string> m_OutgoingQueue;

    // Debugger State
    struct BreakpointInfo {
        std::string file;
        int line;
        int id;
        bool verified;
    };

    // File path -> List of requested breakpoints
    std::map<std::string, std::vector<BreakpointInfo>> m_RequestedBreakpoints;

    // Code Pointer -> Breakpoint ID
    std::unordered_map<unsigned char*, int> m_ActiveBreakpoints;

    // Stepping
    enum StepMode {
        STEP_NONE,
        STEP_IN,
        STEP_OVER,
        STEP_OUT
    };

    struct ThreadState {
        StepMode stepMode;
        int stepDepth; // Stack depth when stepping started
        int startLine; // Line number when stepping started
        bool paused;
        std::string pauseReason;
        unsigned char* ignoreBreakpointAddr;
    };

    std::unordered_map<ScriptVM*, ThreadState> m_ThreadStates;

    // VM Registry
    std::mutex m_VMLock;
    std::map<int, ScriptVM*> m_IDToVM;
    std::map<ScriptVM*, int> m_VMToID;
    int m_NextVMID = 1;

    // Variable references for "variables" request
    // Map refId -> ScriptVariable pointer/context
    // Since ScriptVariables change, we might need a snapshot or careful handling.
    // For now, we will store pointers and hope they are valid between "scopes" and "variables" request (which happens in paused state)
    // Ref 1 -> Locals of current frame
    // Ref 2 -> Globals
    // Ref 3 -> Level
    // Ref 4 -> Game
    // Dynamic Refs > 1000
    std::vector<ScriptVariable*> m_VariableReferences;
    std::mutex m_VarLock;

    // Singleton instance
    static DAPServer* s_Instance;
};

extern DAPServer g_DAPServer;

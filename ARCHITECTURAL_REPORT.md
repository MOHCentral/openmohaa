# OpenMoHAA Scripting Engine Analysis

## 1. Core Architecture

The scripting engine in OpenMoHAA is a custom virtual machine (VM) designed to execute scripts written in the game's `.scr` format. It is deeply integrated with the game's event system and entity management.

### Key Components

*   **`GameScript`**: Represents a parsed and compiled script file. It contains the bytecode (`m_ProgBuffer`), string tables, and labels. It uses a `Compiler` (likely defined in `scriptcompiler.h`) to parse source code into bytecode.
*   **`ScriptThread`**: Represents a running instance of a script. It manages the execution context, including the VM state (`ScriptVM`), local variables, and timing. It inherits from `Listener`, allowing it to receive events.
*   **`ScriptVM`**: The Virtual Machine interpreter. It executes opcodes, manages the stack (`ScriptVMStack`), and handles function calls (`ScriptCallStack`). It supports thread suspension (`STATE_SUSPENDED`), waiting (`STATE_WAITING`), and running (`STATE_RUNNING`).
*   **`ScriptMaster` (Director)**: A singleton (global `Director`) that manages all `GameScript`s and `ScriptThread`s. It handles the loading of scripts, creation of threads, and the main execution loop (`ExecuteRunning`) which steps through active threads.
*   **`ScriptThreadLabel`**: A handle to a specific entry point in a script (File + Label). It is used to pass callback functions or jump targets around.
*   **`Listener`**: The base class for almost everything in the game (Entities, ScriptThreads, etc.). It implements the event system (`ProcessEvent`).
*   **`Event`**: Represents a command or message sent to a `Listener`. Scripts trigger events on entities (e.g., `$player.give("weapon")`), and the engine triggers events on scripts.

## 2. Execution Flow

### Virtual Machine Processing
The VM is stack-based. Instructions are opcodes defined in `scriptopcodes.h` (implied).

1.  **Loading**: `ScriptMaster` loads a `.scr` file into a `GameScript` object. The source is preprocessed and compiled into bytecode.
2.  **Thread Creation**: When a script is started (e.g., via `exec` command or map load), `ScriptMaster::CreateScriptThread` creates a `ScriptThread`.
3.  **Execution Loop**:
    *   `ScriptMaster::ExecuteRunning` iterates through all active threads.
    *   For each thread, `ScriptVM::Execute` is called.
    *   The VM fetches opcodes from `m_ProgBuffer` via `m_CodePos`.
    *   It executes the instruction (e.g., push variable, call function, invoke event on entity).
    *   If a `wait` or `waittill` opcode is encountered, the thread state changes to `THREAD_WAITING` or `THREAD_SUSPENDED`, and execution yields back to the engine until the next frame or event.

### Event System
The `Listener` class allows objects to register callbacks for specific events.
*   **From Script to C++**: When a script calls a function like `iprintln "Hello"`, the VM creates an `Event` object with the name "iprintln" and arguments. It then calls `ProcessEvent` on the target `Listener` (usually the thread itself or an entity). The `Listener` looks up the event in its class declaration (macros like `CLASS_DECLARATION`) and calls the corresponding C++ member function (e.g., `ScriptThread::IPrintln`).
*   **From C++ to Script**: The engine can post events to `ScriptThread`s. The thread processes these events, which might resume execution or trigger a specific script callback.

### Command Execution Path (`iprintln`)
1.  **Script**: `iprintln "Hello"`
2.  **VM**: Parses opcode for command call. Creates `Event("iprintln")`. Adds argument "Hello".
3.  **Dispatch**: Calls `ScriptThread::ProcessEvent(event)`.
4.  **Lookup**: `ScriptThread` checks its event map. Finds `EV_ScriptThread_IPrintln`.
5.  **Execution**: Calls `ScriptThread::IPrintln(event)`.
6.  **Function**: `ScriptThread::IPrintln` retrieves the string from the event and calls `gi.SendServerCommand` to send it to clients.

## 3. Evaluation and Quality

### Strengths
*   **Integration**: Tightly coupled with the game engine, allowing easy manipulation of entities.
*   **State Management**: Built-in support for latent execution (`wait`, `waittill`) makes writing game logic (cinematics, AI) straightforward.

### Weaknesses & Code Smells
*   **Global State**: Reliance on global `Director` and static lists.
*   **String Handling**: Heavy usage of string-based lookups for events and variables, which can be slow.
*   **Memory Management**: Manual memory management with `new`/`delete` and custom allocators (`MEM_BlockAlloc`). `SafePtr` helps, but potential for leaks or use-after-free exists if not careful.
*   **Legacy Code**: Presence of `ScriptDeprecated` calls suggests technical debt.
*   **Buffer Overflows**: Use of fixed-size buffers (`char buffer[1023]`) in `ScriptThreadLabel::Set` and other places poses overflow risks if script inputs are not strictly validated, though `Q_strncpyz` is often used.

### Danger Zones
*   **`m_scriptCmds`**: Currently appears unused/broken.
*   **Event Names**: Collision between script variable names and event names can be confusing.
*   **Threading**: The scripting engine is likely single-threaded within the main game loop. Adding real threads or async operations outside this model would break it.
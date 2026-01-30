# OpenMOHAA Scripting Engine Analysis

## 1. Core Architecture Location

The OpenMOHAA scripting engine is a stack-based Virtual Machine (VM) integrated deeply into the game module (`fgame`).

*   **Virtual Machine (VM):**
    *   **File:** `code/script/scriptvm.cpp` & `code/script/scriptvm.h`
    *   **Class:** `ScriptVM`
    *   **Function:** `ScriptVM::Execute` is the heart of the engine, containing the main switch statement that processes opcodes (e.g., `OP_EXEC_CMD`, `OP_PUSH`, `OP_JUMP`).
    *   **Stack:** `ScriptVMStack` manages `ScriptVariable` objects.

*   **Event System:**
    *   **Files:** `code/corepp/listener.h`, `code/corepp/listener.cpp`
    *   **Classes:** `Event`, `Listener`, `EventDef`.
    *   **Mechanism:** The engine uses a unified Event system for both internal game logic and scripting. A script command (like `iprintln`) is converted into an `Event` object and dispatched to a `Listener` (usually an `Entity` or the `Game` singleton).

*   **Binding (Macro Magic):**
    *   **File:** `code/corepp/class.h`
    *   **Macro:** `CLASS_DECLARATION( ParentClass, ClassName, "ScriptID" )`
    *   **Function:** This macro creates a static `ClassDef` that registers the class and its event handlers.
    *   **ResponseDef:** Maps an `Event` type (e.g., `EV_ConsoleCommand`) to a member function (e.g., `&Player::ConsoleCommand`).

*   **Script Management:**
    *   **File:** `code/fgame/scriptmaster.cpp` & `code/fgame/scriptmaster.h`
    *   **Class:** `ScriptMaster` (Global instance: `Director`)
    *   **Function:** Manages loading scripts, creating `ScriptThread`s, and handling global script events.

## 2. Architectural Map

### VM Execution Flow
The VM operates on `ScriptThread` objects. Each thread has a `ScriptVM` instance.

1.  **Parsing:** The `ScriptCompiler` (`code/script/scriptcompiler.cpp`) compiles `.scr` text files into bytecode (opcodes).
2.  **Execution:** `ScriptVM::Execute` runs the bytecode.
3.  **Command Processing:**
    *   When the VM encounters a command opcode (e.g., `OP_EXEC_CMD`), it looks up the command name in the `Event` system.
    *   It creates an `Event` object and pushes arguments from the VM stack into the Event.
    *   It calls `listener->ProcessEvent(event)`.

### From Script to C++: The Journey of `iprintln "Hello"`

1.  **Script File:** `iprintln "Hello"`
2.  **Compiler:** Converts `iprintln` to an opcode (e.g., `OP_EXEC_CMD`) and "Hello" to a string index.
3.  **VM Execution (`ScriptVM::Execute`):**
    *   Fetches opcode `OP_EXEC_CMD`.
    *   Fetches event ID for `iprintln`.
    *   Pops "Hello" from stack.
    *   Constructs `Event` (Name: "iprintln", Arg: "Hello").
    *   Calls `m_Thread->ProcessEvent(evt)` (or `Director.ProcessEvent(evt)`).
4.  **Dispatch (`Listener::ProcessEvent`):**
    *   Looks up "iprintln" in the `ClassDef` of the listener.
    *   Finds the function pointer (e.g., `&ScriptThread::Print`).
5.  **C++ Execution:**
    *   Executes `void ScriptThread::Print(Event *ev)`.
    *   Function retrieves arg: `ev->GetString(1)`.
    *   Prints to console.

## 3. Quality & Danger Zones

### Legacy Code Smells
*   **Buffer Handling:** Uses many fixed-size buffers (e.g., `char text[1024]` in `G_Printf`).
    *   *Risk:* Classic stack overflow if script inputs exceed limits.
*   **Global State:** Heavily relies on global variables (`Director`, `level`, `game`) and `gi` (Game Import) pointers.
    *   *Risk:* Not thread-safe. Threading must be handled carefully.
*   **Raw Pointers:** The Event system passes raw `Event *` pointers. Ownership is generally transferred to the processed function or deleted immediately after, but it's fragile.

### Danger Zones for New Features
*   **Threading:** The engine is **single-threaded**.
    *   *Constraint:* You **cannot** access `ScriptVariable`, `Event`, or `Listener` objects from a background thread. You must marshall data back to the main thread.
*   **Memory Management:** `ScriptVariable`s are reference counted or copied.
    *   *Constraint:* Do not hold references to `ScriptVariable`s in background threads; they might be garbage collected or modified by the main thread.
*   **Stack Depth:** The VM has a recursion limit (`MAX_STACK_DEPTH`). Deeply nested callbacks could trigger this.

## Critical Path: `iprintln`
`ScriptVM::Execute` -> `OP_EXEC_CMD` -> `Event(ev)` -> `Listener::ProcessEvent` -> `Lookup Handler` -> `Handler()`

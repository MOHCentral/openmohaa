#include <iostream>
#include <string>
#include <vector>
#include <cstring>

// Include mocks
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/shader_material.hpp>

// Define global variable for capturing error
std::string g_mock_printerr_last;

namespace godot {
    void UtilityFunctions::print(const String &msg) {
        std::cout << "[MOCK PRINT] " << msg.utf8() << std::endl;
    }
    void UtilityFunctions::printerr(const String &msg) {
        std::cerr << "[MOCK PRINTERR] " << msg.utf8() << std::endl;
        g_mock_printerr_last = msg.utf8();
    }
}

// Mocks for C functions
extern "C" {
    long Godot_VFS_ReadFile(const char *qpath, void **out_buffer) {
        std::cout << "[MOCK VFS] ReadFile: " << qpath << std::endl;
        // Mock failure
        if (out_buffer) *out_buffer = nullptr;
        return 0; // 0 or negative indicates failure in engine
    }
    void Godot_VFS_FreeFile(void *buffer) {}

    // We don't need these for the failure path, but might need symbols
    char **Godot_VFS_ListFiles(const char *directory, const char *extension, int *out_count) {
        if (out_count) *out_count = 0;
        return nullptr;
    }
    void Godot_VFS_FreeFileList(char **list) {}
}

// Stubs for ShaderProps
#include "godot_shader_props.h"

void Godot_ShaderProps_Load() {
    std::cout << "[MOCK ShaderProps] Load" << std::endl;
}
void Godot_ShaderProps_Unload() {}
const GodotShaderProps *Godot_ShaderProps_Find(const char *shader_name) { return nullptr; }

// Stub for ShaderMaterial builder
#include "godot_shader_material.h"
godot::Ref<godot::ShaderMaterial> Godot_Shader_BuildMaterial(const GodotShaderProps *props) {
    return godot::Ref<godot::ShaderMaterial>();
}

// Include the source file under test
// We suppress some warnings because we are including a cpp file directly
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../godot_bsp_mesh.cpp"
#pragma GCC diagnostic pop

int main() {
    std::cout << "Running test_bsp_load_failure..." << std::endl;

    // Reset error capture
    g_mock_printerr_last = "";

    // Call the function
    const char* bsp_path = "maps/test.bsp";
    godot::Node3D* result = Godot_BSP_LoadWorld(bsp_path);

    // Verify
    bool passed = true;
    if (result != nullptr) {
        std::cerr << "FAILED: Godot_BSP_LoadWorld returned non-null for failed VFS read." << std::endl;
        passed = false;
    } else {
        std::cout << "SUCCESS: Godot_BSP_LoadWorld returned nullptr." << std::endl;
    }

    // Check if error was logged
    if (g_mock_printerr_last.find("Failed to read") == std::string::npos) {
        std::cerr << "FAILED: Expected error message not printed. Got: '" << g_mock_printerr_last << "'" << std::endl;
        passed = false;
    } else {
        std::cout << "SUCCESS: Error message verified." << std::endl;
    }

    if (passed) {
        std::cout << "ALL TESTS PASSED" << std::endl;
        return 0;
    } else {
        return 1;
    }
}

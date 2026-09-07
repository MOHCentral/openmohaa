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

#include "../str.h"

#include <iostream>
#include <csignal>
#include <cstdlib>

#ifdef __unix__
#include <unistd.h>
#include <sys/wait.h>
#endif

void test_cap_length() {
    str s("hello");
    s.CapLength(3);
    if (s != "hel") {
        std::cerr << "CapLength(3) failed: " << s.c_str() << std::endl;
        exit(1);
    }

    s.CapLength(5); // Should do nothing since length is 3
    if (s != "hel") {
        std::cerr << "CapLength(5) failed: " << s.c_str() << std::endl;
        exit(1);
    }

    std::cout << "CapLength basic tests passed" << std::endl;
}

// Test the assertion using death tests manually
bool test_cap_length_assertion() {
#ifdef __unix__
    pid_t pid = fork();
    if (pid == 0) {
        str s;
        // A default-constructed `str` has `m_data = NULL`.
        // CapLength will trigger `assert(m_data)`.
        s.CapLength(3);
        exit(0);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        // If assertion failed, the process should be killed by SIGABRT
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
            std::cout << "CapLength assertion test passed" << std::endl;
            return true;
        } else {
            std::cerr << "CapLength assertion test failed! Expected SIGABRT, got status: " << status << std::endl;
            return false;
        }
    }
    return false;
#else
    // Skip death test on non-unix platforms
    std::cout << "Skipping CapLength assertion test on non-unix platform" << std::endl;
    return true;
#endif
}

int main(int argc, char *argv[]) {
    test_cap_length();

#ifndef NDEBUG
    if (!test_cap_length_assertion()) {
        std::cerr << "Assertion test for CapLength on null str failed (expected SIGABRT)" << std::endl;
        return 1;
    }
#endif

    std::cout << "All tests passed!" << std::endl;
    return 0;
}

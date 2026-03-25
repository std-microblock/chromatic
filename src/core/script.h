#pragma once
#include <optional>
#include <string>
#include "breeze-js/script.h"

namespace chromatic::script {
struct runtime {
    breeze::script_context context;
    void run_loop_block();
    void cleanup();
    void reset();
};

}; // namespace chromatic
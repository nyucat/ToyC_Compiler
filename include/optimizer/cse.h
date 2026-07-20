#pragma once

#include "ir/ir.h"

namespace toyc::optimizer {

class CsePass {
public:
    void run(toyc::ir::IRModule& module);
};

} // namespace toyc::optimizer

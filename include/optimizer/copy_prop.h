#pragma once

#include "ir/ir.h"

namespace toyc::optimizer {

class CopyPropPass {
public:
    void run(toyc::ir::IRModule& module);
};

} // namespace toyc::optimizer

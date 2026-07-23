#pragma once

#include "ir/ir.h"

namespace toyc::optimizer {

void runPeepholeOptimizations(toyc::ir::IRModule& module);

}

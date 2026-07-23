#pragma once

#include "ir/basic_block.h"

namespace toyc::optimizer {

void runPeepholeOptimizations(toyc::ir::IRModule& module);

}

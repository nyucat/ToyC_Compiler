#pragma once

#include "sema/symbol.h"

#include <string>
#include <vector>

namespace toyc::sema {

struct GlobalObjectInfo {
    ast::Symbol* symbol = nullptr;
    std::string name;
    bool isConst = false;
    int initialValue = 0;
};

struct SemanticResult {
    bool success = true;
    std::vector<GlobalObjectInfo> globalObjects;
    ast::Symbol* mainFunction = nullptr;
};

} // namespace toyc::sema

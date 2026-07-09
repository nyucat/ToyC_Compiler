#pragma once

#include <stdexcept>
#include <string>

namespace toyc::sema {

class SemanticError : public std::runtime_error {
public:
    explicit SemanticError(const std::string& message) : std::runtime_error(message) {}
};

} // namespace toyc::sema

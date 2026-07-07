#pragma once

#include "ir/ir.h"

#include <string>
#include <vector>

namespace toyc::ir {

class BasicBlock {
public:
    explicit BasicBlock(std::string label);

    [[nodiscard]] const std::string& label() const { return label_; }
    [[nodiscard]] const std::vector<IRInstruction>& instructions() const { return instructions_; }
    [[nodiscard]] std::vector<IRInstruction>& instructions() { return instructions_; }
    [[nodiscard]] const std::vector<BasicBlock*>& predecessors() const { return predecessors_; }
    [[nodiscard]] const std::vector<BasicBlock*>& successors() const { return successors_; }

    void addInstruction(IRInstruction instruction);
    void addPredecessor(BasicBlock* block);
    void addSuccessor(BasicBlock* block);
    void clearEdges();
    [[nodiscard]] bool isTerminated() const;
    [[nodiscard]] const IRInstruction* terminator() const;

private:
    std::string label_;
    std::vector<IRInstruction> instructions_;
    std::vector<BasicBlock*> predecessors_;
    std::vector<BasicBlock*> successors_;
};

void buildCFG(IRFunction& function);
[[nodiscard]] std::vector<BasicBlock*> reversePostOrder(IRFunction& function);

} // namespace toyc::ir

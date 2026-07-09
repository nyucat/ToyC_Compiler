#include "optimizer/cfg_simplify.h"

#include "ir/basic_block.h"

#include <map>
#include <unordered_map>
#include <unordered_set>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

void jumpThreading(IRFunction& function) {
    buildCFG(function);

    std::map<std::string, BasicBlock*> labels;
    for (auto& block : function.blocks) {
        labels.emplace(block.label(), &block);
    }

    for (auto& block : function.blocks) {
        if (block.instructions().size() != 1) {
            continue;
        }
        IRInstruction& term = block.instructions().front();
        if (term.op != IROp::Branch) {
            continue;
        }

        auto nextIt = labels.find(term.label);
        if (nextIt == labels.end() || nextIt->second->instructions().size() != 1) {
            continue;
        }
        const IRInstruction& nextTerm = nextIt->second->instructions().front();
        if (nextTerm.op == IROp::Branch) {
            term.label = nextTerm.label;
        }
    }

    // Follow branch chains to the final target.
    for (auto& block : function.blocks) {
        for (auto& inst : block.instructions()) {
            auto resolveTarget = [&](std::string label) {
                std::unordered_set<std::string> visited;
                while (visited.insert(label).second) {
                    auto it = labels.find(label);
                    if (it == labels.end() || it->second->instructions().size() != 1) {
                        return label;
                    }
                    const IRInstruction& hop = it->second->instructions().front();
                    if (hop.op != IROp::Branch) {
                        return label;
                    }
                    label = hop.label;
                }
                return label;
            };

            if (inst.op == IROp::Branch) {
                inst.label = resolveTarget(inst.label);
            }
        }
    }

    buildCFG(function);
}

void mergeBlocks(IRFunction& function) {
    buildCFG(function);

    for (std::size_t i = 0; i + 1 < function.blocks.size();) {
        BasicBlock& current = function.blocks[i];
        if (current.successors().size() != 1) {
            ++i;
            continue;
        }

        BasicBlock* succ = current.successors().front();
        if (succ->predecessors().size() != 1 || succ == &current) {
            ++i;
            continue;
        }

        if (!current.isTerminated() || current.terminator()->op != IROp::Branch ||
            current.terminator()->label != succ->label()) {
            ++i;
            continue;
        }

        current.instructions().pop_back();
        auto succInsts = succ->instructions();
        current.instructions().insert(current.instructions().end(), succInsts.begin(), succInsts.end());

        const std::string removedLabel = succ->label();
        function.blocks.erase(function.blocks.begin() + static_cast<std::ptrdiff_t>(i + 1));

        for (auto& block : function.blocks) {
            for (auto& inst : block.instructions()) {
                if (inst.op == IROp::Branch && inst.label == removedLabel) {
                    inst.label = current.label();
                }
                if (inst.op == IROp::CondBranch) {
                    if (inst.trueLabel == removedLabel) {
                        inst.trueLabel = current.label();
                    }
                    if (inst.falseLabel == removedLabel) {
                        inst.falseLabel = current.label();
                    }
                }
            }
        }

        buildCFG(function);
    }
}

void copyPropagation(IRFunction& function) {
    (void)function;
}

} // namespace

void CfgSimplifyPass::run(IRModule& module) {
    for (auto& function : module.functions) {
        copyPropagation(function);
        jumpThreading(function);
    }
}

} // namespace toyc::optimizer

#include "optimizer/cfg_simplify.h"

#include "ir/basic_block.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace toyc::optimizer {

namespace {

using namespace toyc::ir;

void jumpThreading(IRFunction& function) {
    buildCFG(function);

    std::unordered_map<std::string, BasicBlock*> labels;
    labels.reserve(function.blocks.size());
    for (auto& block : function.blocks) {
        labels.emplace(block.label(), &block);
    }

    // Single-pass: resolve all branch-through-branch shortcuts
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

    // Follow branch chains to the final target with path compression
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
            } else if (inst.op == IROp::CondBranch) {
                inst.trueLabel = resolveTarget(inst.trueLabel);
                inst.falseLabel = resolveTarget(inst.falseLabel);
            }
        }
    }

    buildCFG(function);
}

void mergeBlocks(IRFunction& function) {
    if (function.blocks.empty()) {
        return;
    }

    // Build label-to-index map (jumpThreading already called buildCFG)
    std::unordered_map<std::string, std::size_t> labelIndex;
    labelIndex.reserve(function.blocks.size());
    for (std::size_t i = 0; i < function.blocks.size(); ++i) {
        labelIndex.emplace(function.blocks[i].label(), i);
    }

    // Collect merge pairs: (block i, successor index) where merge is valid
    bool changed = true;
    while (changed) {
        changed = false;

        // Rebuild label index after any erasure
        labelIndex.clear();
        for (std::size_t i = 0; i < function.blocks.size(); ++i) {
            labelIndex.emplace(function.blocks[i].label(), i);
        }

        // Find all merge candidates in one pass
        std::vector<std::pair<std::size_t, std::size_t>> merges;
        std::unordered_set<std::size_t> mergedAway;

        for (std::size_t i = 0; i + 1 < function.blocks.size(); ++i) {
            if (mergedAway.count(i)) continue;

            BasicBlock& current = function.blocks[i];
            if (!current.isTerminated() || current.terminator()->op != IROp::Branch) {
                continue;
            }

            const auto succIt = labelIndex.find(current.terminator()->label);
            if (succIt == labelIndex.end()) continue;
            const std::size_t succIdx = succIt->second;

            if (mergedAway.count(succIdx)) continue;

            BasicBlock& succ = function.blocks[succIdx];
            // Only merge if successor has exactly one predecessor (the current block)
            if (succ.predecessors().size() != 1 || succIdx == i) {
                continue;
            }

            merges.emplace_back(i, succIdx);
            mergedAway.insert(succIdx);
            changed = true;
        }

        if (merges.empty()) break;

        // Apply all merges
        // Sort by index descending so erasures don't shift earlier indices
        std::sort(merges.begin(), merges.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        // Collect label renames: removedLabel -> newLabel
        std::unordered_map<std::string, std::string> labelRenames;

        for (const auto& [blockIdx, succIdx] : merges) {
            BasicBlock& current = function.blocks[blockIdx];
            BasicBlock& succ = function.blocks[succIdx];

            const std::string removedLabel = succ.label();
            const std::string keepLabel = current.label();

            // Remove the branch terminator from current, append all succ instructions
            current.instructions().pop_back();
            auto& succInsts = succ.instructions();
            current.instructions().insert(
                current.instructions().end(),
                std::make_move_iterator(succInsts.begin()),
                std::make_move_iterator(succInsts.end()));

            labelRenames[removedLabel] = keepLabel;
        }

        // Erase merged blocks (descending order preserves indices)
        for (const auto& [_, succIdx] : merges) {
            function.blocks.erase(function.blocks.begin() + static_cast<std::ptrdiff_t>(succIdx));
        }

        // Apply label renames across all remaining instructions
        for (auto& block : function.blocks) {
            for (auto& inst : block.instructions()) {
                if (inst.op == IROp::Branch) {
                    auto it = labelRenames.find(inst.label);
                    if (it != labelRenames.end()) {
                        inst.label = it->second;
                    }
                } else if (inst.op == IROp::CondBranch) {
                    auto itT = labelRenames.find(inst.trueLabel);
                    if (itT != labelRenames.end()) inst.trueLabel = itT->second;
                    auto itF = labelRenames.find(inst.falseLabel);
                    if (itF != labelRenames.end()) inst.falseLabel = itF->second;
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
        mergeBlocks(function);
    }
}

} // namespace toyc::optimizer

#include "ir/basic_block.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace toyc::ir {

BasicBlock::BasicBlock(std::string label) : label_(std::move(label)) {}

void BasicBlock::addInstruction(IRInstruction instruction) {
    if (isTerminated()) {
        throw std::runtime_error("cannot append instruction after terminator in block " + label_);
    }
    instructions_.push_back(std::move(instruction));
}

void BasicBlock::addPredecessor(BasicBlock* block) {
    if (std::find(predecessors_.begin(), predecessors_.end(), block) == predecessors_.end()) {
        predecessors_.push_back(block);
    }
}

void BasicBlock::addSuccessor(BasicBlock* block) {
    if (std::find(successors_.begin(), successors_.end(), block) == successors_.end()) {
        successors_.push_back(block);
    }
}

void BasicBlock::clearEdges() {
    predecessors_.clear();
    successors_.clear();
}

bool BasicBlock::isTerminated() const {
    return !instructions_.empty() && terminator() != nullptr;
}

const IRInstruction* BasicBlock::terminator() const {
    if (instructions_.empty()) {
        return nullptr;
    }
    const auto& last = instructions_.back();
    switch (last.op) {
    case IROp::Branch:
    case IROp::CondBranch:
    case IROp::Return:
        return &last;
    default:
        return nullptr;
    }
}

void buildCFG(IRFunction& function) {
    std::unordered_map<std::string, BasicBlock*> labelMap;
    labelMap.reserve(function.blocks.size());
    for (auto& block : function.blocks) {
        block.clearEdges();
        labelMap.emplace(block.label(), &block);
    }

    for (auto& block : function.blocks) {
        const IRInstruction* term = block.terminator();
        if (term == nullptr) {
            continue;
        }

        if (term->op == IROp::Branch) {
            auto it = labelMap.find(term->label);
            if (it != labelMap.end()) {
                block.addSuccessor(it->second);
                it->second->addPredecessor(&block);
            }
        } else if (term->op == IROp::CondBranch) {
            auto trueIt = labelMap.find(term->trueLabel);
            if (trueIt != labelMap.end()) {
                block.addSuccessor(trueIt->second);
                trueIt->second->addPredecessor(&block);
            }
            auto falseIt = labelMap.find(term->falseLabel);
            if (falseIt != labelMap.end()) {
                block.addSuccessor(falseIt->second);
                falseIt->second->addPredecessor(&block);
            }
        }
    }
}

std::vector<BasicBlock*> reversePostOrder(IRFunction& function) {
    buildCFG(function);
    if (function.blocks.empty()) {
        return {};
    }

    std::unordered_map<BasicBlock*, int> indexOf;
    for (std::size_t i = 0; i < function.blocks.size(); ++i) {
        indexOf.emplace(&function.blocks[i], static_cast<int>(i));
    }

    std::unordered_set<BasicBlock*> visited;
    std::vector<BasicBlock*> postOrder;
    postOrder.reserve(function.blocks.size());

    const auto dfs = [&](auto&& self, BasicBlock* block) -> void {
        if (block == nullptr || visited.count(block) > 0) {
            return;
        }
        visited.insert(block);
        std::vector<BasicBlock*> succ = block->successors();
        std::sort(succ.begin(), succ.end(), [&](BasicBlock* lhs, BasicBlock* rhs) {
            return indexOf[lhs] < indexOf[rhs];
        });
        for (BasicBlock* next : succ) {
            self(self, next);
        }
        postOrder.push_back(block);
    };

    dfs(dfs, &function.blocks.front());
    std::reverse(postOrder.begin(), postOrder.end());
    return postOrder;
}

} // namespace toyc::ir

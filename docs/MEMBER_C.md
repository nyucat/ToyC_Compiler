# 成员 C 模块说明

本目录由成员 C 负责，包含 IR 生成、控制流构建与中端优化。

## 交付物

```
include/ir/
src/ir/
include/optimizer/
src/optimizer/
tests/ir/
tests/optimization/
scripts/run_ir_tests.ps1
cmake/ir_integration.cmake
```

## 与其他成员的接口

- **输入（来自 B）**：已完成 `resolvedSymbol` 绑定的 AST
- **输出（交给 D）**：

```cpp
struct IRModule {
    std::vector<IRGlobal> globals;
    std::vector<IRFunction> functions;
};
```

- **依赖（来自 B）**：`include/sema/symbol.h` 中的 `Symbol` 结构定义

## 不属于成员 C 的改动

以下文件由其他成员负责，C 分支不应修改：

| 文件 | 负责人 |
|------|--------|
| `src/main.cpp` | A |
| `CMakeLists.txt`（主构建） | A |
| `src/frontend/*` | A |
| `src/sema/semantic_analyzer.*` | B |
| `src/sema/scope.*` | B |

联调时由 A 在 `main.cpp` 中调用：

```cpp
toyc::ir::IRModule module;
toyc::ir::IRBuilder builder(module);
builder.buildCompUnit(*program);
toyc::optimizer::runOptimizationPipeline(module, options.optimize);
```

## 独立测试

在已合并成员 A 前端代码的前提下：

```powershell
cmake -S . -B out/build/x64-Debug -G "Visual Studio 17 2022" -A x64
cmake --build out/build/x64-Debug --config Debug
ctest --test-dir out/build/x64-Debug -C Debug --output-on-failure
```

`cmake/ir_integration.cmake` 会自动注册 `test_ir` 与 `test_optimizer`。

#  Bug 收集仓库操作规范

## 一、需要提交什么

- 按项目开发阶段收集 Bug，要求覆盖前期、中期、后期。
- 每组提交5个可稳定复现的有效bug**（开发过程中真实出现的，不可编造）**。每个 Bug 必须形成一条完整链路：

```text
Issue -> 修复分支 -> 失败测试 -> 修复代码 -> PR -> 合入 -> 归档记录
```

最终仓库中至少应包含：

1. GitHub Issue：描述 Bug、复现步骤、错误行为、预期行为；
2. 测试文件：包含修复前失败、修复后通过的断言；
3. 修复代码：只包含与该 Bug 相关的最小修改；
4. Pull Request：关联对应 Issue，说明修复和验证结果；
5. 归档记录：在 `commits-log.xlsx` 中记录 Issue、PR、commit、测试文件、修复文件的对应关系。

## 二、Bug 收集标准

1. Bug 必须是源码逻辑缺陷，不记录配置、环境、工具使用类问题；
2. Bug 必须可稳定复现，提供最小化 buggy 方法或最小化复现代码；
3. Fix 修复代码仅保留缺陷相关修改，不混入无关功能、格式调整；
4. 测试必须包含明确断言，能够证明修复前程序行为错误，并且修复后程序可通过测试；
5. 每个 Bug 必须标注测试类型：单元测试 / 集成测试；
6. 整期至少提供 1 个集成测试；如果项目确实不适合写集成测试，需要在 Issue 中说明原因；
7. Issue、PR、commit、归档文件中的编号必须一致，便于反向追溯。

## 三、仓库创建与权限要求

### 3.1 仓库可见性

仓库必须设置为私有（Private）。如果仓库尚未私有化，应在提交 Issue 前先改为 Private，并一直保持私有化。

### 3.2 合作者配置

在 GitHub 仓库的 `Settings -> Collaborators` 中添加本组成员和下列课程助教。

| 助教姓名 | Github用户名 |
| --- | --- |
| 刘心雨 | give-to |
| 凡桂宏   | ILsoMachine |

权限建议：

1. 组长：维护者或管理员权限，负责审核、合并、统计；
2. 组员、助教：读写权限，可提 Issue、提交代码、创建 PR；

协作约定：禁止直接推送代码至 `main` 或 `master`，所有修复通过 PR 提交审核。

## 四、完整工作流

### 4.1 流程说明

1. Issue：记录问题是什么、如何复现、为什么是 Bug；
2. 失败测试：用自动化测试证明 Bug 存在；
3. 修复分支：把每个 Bug 的修复隔离开，避免互相影响；
4. Commit：记录测试和修复的开发过程；
5. PR：把 Issue、测试、修复、验证结果集中呈现，供组长或助教审核；
6. 合入与归档：把 Issue、PR、commit、测试和修复代码的对应关系归档到`commits-log.xlsx`中。

### 4.2 第一步：提交 Issue

Issue 必须包含以下内容：

1. Bug 分类：源码逻辑缺陷；
2. 开发阶段：前期 / 中期 / 后期；
3. Issue 编号和修复分支：PR合并后补充；
5. 编译和运行环境；
6. 测试类型：单元测试 / 集成测试；
7. 最小化 buggy 方法
7. 最小化测试代码；
8. 自动化复现步骤：服务于自动化测试，写清楚如何构建、如何运行测试、哪个测试失败、失败断言是什么；
9. 观察到的错误行为；
10. 预期正确行为。

Issue 模板：

````markdown
## 【Bug分类】源码逻辑缺陷｜开发阶段：前期/中期/后期

## 关联信息（PR合并后补充）
1. Issue编号：#xxx
2. 预计修复分支：fix/issue-xxx-缺陷简述

## 基础环境
1. 编译环境：OS、编译器版本、构建脚本参数

## 测试类型
单元测试/集成测试

## 最小化 buggy 方法
粘贴能够触发缺陷的最小源码片段，剔除无关业务逻辑。

## 自动化测试断言
粘贴修复前会失败、修复后会通过的测试断言。

## 复现步骤
1. 执行构建命令：xxx
2. 执行测试命令：xxx
3. 测试输入为xxx，执行xxx分支，应该返回xxx，实际返回xxx，导致断言失败

## 观察到的错误行为
粘贴测试失败日志、错误返回值、崩溃栈或错误输出。

## 预期正确行为
描述修复后应当通过的断言或正确输出。
````

Issue 示例：

````markdown
标题：【中期 - 源码逻辑 Bug】常量折叠编译优化分支判断失效

## 【Bug分类】源码逻辑缺陷｜开发阶段：中期

## 关联信息（PR合并后补充）
1. Issue编号：#1
2. 预计修复分支：fix/issue-001-const-fold-error

## 基础环境
1. 编译环境：Ubuntu 22.04，g++ 11.4

## 测试类型
单元测试

## 最小化 buggy 方法
```cpp
// src/calc.cpp
int calc(int a) {
    if (a < 5) {
        return 0; // 这里应返回 1
    }
    return 0;
}
```

## 自动化测试断言
```cpp
// test/test_const_fold.cpp
void test_calc_const_fold_branch() {
    int actual = calc(3);
    assert(actual == 1);
}
```

## 复现步骤
1. 执行构建命令：`g++ src/calc.cpp test/test_const_fold.cpp -o test_buggy`
2. 执行测试命令：`./test_buggy`
3. 测试 `test_calc_const_fold_branch` 中 `assert(actual == 1)` 失败，测试输入为`3`，进入`if`分支，应该返回`1`，但是`calc(3)` 实际返回 `0`，导致断言失败

## 观察到的错误行为
`calc(3)` 返回 `0`，导致断言失败。

## 预期正确行为
`a=3` 时满足 `a < 5`，函数应返回 `1`，测试应通过。
````

### 4.3 第二步：创建修复分支

每个 Bug 单独创建一个修复分支。分支编号必须与 Issue 编号一致。例如：Issue #1 对应 fix/issue-001-const-fold-error。

```bash
git checkout main
git pull
git checkout -b fix/issue-001-const-fold-error
```

如果仓库主分支是 `master`，则把 `main` 替换为 `master`。

### 4.4 第三步：提交失败测试和修复代码

一个 Bug 拆成 2 次 commit：

第一 commit：新增测试用例，测试在修复前失败。

```bash
git add test/test_const_fold.cpp
git commit -m "[Issue #1] add failing test for const fold bug"
```

第二 commit：提交源码修复，测试在修复后通过。

```bash
git add src/calc.cpp
git commit -m "[Issue #1] fix const fold branch logic"
```

所有 commit message 都必须包含 Issue 编号。

### 4.5 第四步：提交 PR

推送修复分支：

```bash
git push origin fix/issue-001-const-fold-error
```

PR 要求：

1. 一个 Issue 强制对应一个 PR；
2. PR 标题格式：`Fix Issue #编号：缺陷简述`；
3. PR 描述必须写明关联 Issue、修复内容、测试覆盖、验证结果、影响范围、主要 commit；
4. Issue 中补充 PR 链接，PR 中补充 Issue 链接，保证双向可追溯。

PR 描述示例：

```markdown
关联 Issue：Fixes #1

修复内容：修正 `calc` 中分支返回值错误。

测试覆盖：新增 `test_const_fold.cpp`，覆盖 `calc(3)` 场景。

验证结果：
- 修复前：`test_calc_const_fold_branch` 断言失败，`calc(3)` 实际返回 `0`
- 修复后：`test_calc_const_fold_branch` 通过，全部测试通过

影响范围：仅修改该分支判断逻辑，不涉及其他模块。

主要 Commit：
- [Issue #1] add failing test for const fold bug
- [Issue #1] fix const fold branch logic
```

### 4.6 第五步：审核、合入与关闭 Issue

审核校验点：

1. Bug 是否可自动化复现；
2. 最小化 buggy 方法是否无冗余；
3. 修复前测试是否失败；
4. 修复后测试是否通过；
5. 修复代码是否只包含 Bug 相关修改；
6. Issue、PR、commit 编号是否一致；
7. PR 描述是否包含修复前失败和修复后通过的验证结果；
8. 是否标注前期 / 中期 / 后期。

审核通过后建议使用 Squash 合并。Squash 合并信息中保留 Issue 编号，例如：

```text
Fix Issue #1: const fold branch logic error
```

合并完成后，验证修复结果，再关闭 Issue。

## 五、Bug 归档要求

每个 Bug 合入后，明确创建或更新以下 2 类文件：

1. `test/` 中的测试文件：保存该 Bug 对应的自动化测试；
2. `commits-log.xlsx`：保存 Issue、PR、commit、测试文件、修复文件的对应关系。

`commits-log.xlsx` 示例：

| Issue | PR | 阶段 | 分支 | 测试文件 | 修复文件 | 关键 Commit | 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| #1 | #2 | 中期 | fix/issue-001-const-fold-error | test/test_const_fold.cpp | src/calc.cpp | commit hash | 已合入 |

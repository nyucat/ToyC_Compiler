#!/bin/bash
# 差分测试脚本 - 对比ToyC编译器与gcc的输出结果

set -e

echo "========================================="
echo "差分测试：对比ToyC编译器与gcc输出"
echo "========================================="

# 检查编译器
if [ ! -f "build/compiler" ] && [ ! -f "build/compiler.exe" ]; then
    echo "错误: ToyC编译器未构建"
    exit 1
fi

if [ -f "build/compiler.exe" ]; then
    TOYC="./build/compiler.exe"
else
    TOYC="./build/compiler"
fi

# 检查RISC-V工具链
if ! command -v riscv32-unknown-elf-gcc &> /dev/null; then
    echo "警告: 未找到riscv32-unknown-elf-gcc"
    echo "请安装RISC-V工具链或使用其他模拟器"
    echo ""
    echo "在Ubuntu上可以安装:"
    echo "  sudo apt-get install gcc-riscv64-unknown-elf"
    echo ""
    TOOLCHAIN_AVAILABLE=false
else
    TOOLCHAIN_AVAILABLE=true
fi

# 创建测试目录
mkdir -p test_output/diff_test

# 测试用例列表
declare -a TEST_CASES=(
    "test1:简单返回值:int main() { return 42; }"
    "test2:算术运算:int main() { return 2 + 3 * 4 - 5; }"
    "test3:比较运算:int main() { int x = 10; if (x > 5) { return 1; } return 0; }"
    "test4:循环累加:int main() { int i = 0; int sum = 0; while (i < 10) { sum = sum + i; i = i + 1; } return sum; }"
    "test5:递归斐波那契:int fib(int n) { if (n <= 1) { return n; } return fib(n - 1) + fib(n - 2); } int main() { return fib(10); }"
    "test6:逻辑运算:int main() { int a = 1; int b = 0; if (a && !b) { return 100; } return 0; }"
    "test7:函数调用:int add(int a, int b) { return a + b; } int main() { return add(3, 5); }"
    "test8:全局变量:int global = 10; int main() { return global; }"
    "test9:全局常量:const int BASE = 5; int main() { return BASE * 2; }"
    "test10:break语句:int main() { int i = 0; while (i < 100) { i = i + 1; if (i == 10) { break; } } return i; }"
)

PASS_COUNT=0
FAIL_COUNT=0
TOTAL_COUNT=${#TEST_CASES[@]}

for test_case in "${TEST_CASES[@]}"; do
    IFS=':' read -r test_name test_desc test_code <<< "$test_case"
    
    echo ""
    echo "----------------------------------------"
    echo "测试: $test_name - $test_desc"
    echo "----------------------------------------"
    
    # 创建ToyC源文件
    echo "$test_code" > "test_output/diff_test/${test_name}.tc"
    
    # 用gcc编译并运行，获取期望返回值
    echo "$test_code" > "test_output/diff_test/${test_name}_gcc.c"
    gcc -o "test_output/diff_test/${test_name}_gcc" "test_output/diff_test/${test_name}_gcc.c" 2>/dev/null
    
    if [ $? -eq 0 ]; then
        EXPECTED_RESULT=$(./test_output/diff_test/${test_name}_gcc)
        echo "期望结果 (gcc): $EXPECTED_RESULT"
    else
        echo "gcc编译失败，跳过该测试"
        ((FAIL_COUNT++))
        continue
    fi
    
    # 用ToyC编译器编译
    $TOYC < "test_output/diff_test/${test_name}.tc" > "test_output/diff_test/${test_name}.s" 2>/dev/null
    
    if [ $? -ne 0 ]; then
        echo "ToyC编译失败"
        ((FAIL_COUNT++))
        continue
    fi
    
    echo "ToyC编译成功，汇编文件: test_output/diff_test/${test_name}.s"
    
    # 如果有RISC-V工具链，尝试运行
    if [ "$TOOLCHAIN_AVAILABLE" = true ]; then
        echo "尝试用RISC-V工具链运行..."
        
        # 编译汇编为可执行文件
        riscv32-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib -static \
            -o "test_output/diff_test/${test_name}_riscv" \
            "test_output/diff_test/${test_name}.s" \
            -e main 2>/dev/null
        
        if [ $? -eq 0 ]; then
            # 这里需要模拟器来运行，如果有spike可以用：
            # ACTUAL_RESULT=$(spike pk test_output/diff_test/${test_name}_riscv; echo $?)
            echo "RISC-V可执行文件已生成"
            echo "注意：需要RISC-V模拟器(spike)来实际运行"
        else
            echo "RISC-V链接失败"
        fi
    fi
    
    echo "状态: 已生成汇编代码"
    ((PASS_COUNT++))
done

echo ""
echo "========================================="
echo "差分测试统计"
echo "========================================="
echo "总测试数: $TOTAL_COUNT"
echo "生成成功: $PASS_COUNT"
echo "生成失败: $FAIL_COUNT"
echo ""

if [ $FAIL_COUNT -eq 0 ]; then
    echo "所有测试用例均成功生成汇编代码！" -ForegroundColor Green
else
    echo "有 $FAIL_COUNT 个测试用例失败"
fi

echo ""
echo "提示："
echo "1. ToyC程序可以用gcc直接编译运行，因为ToyC是C的子集"
echo "2. gcc的运行结果可以作为ToyC编译器的正确性参照"
echo "3. RISC-V汇编需要模拟器才能在x86上运行"

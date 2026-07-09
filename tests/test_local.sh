#!/bin/bash
# 本地测试脚本 - 用于验证编译器是否符合测评要求

set -e

echo "========================================="
echo "ToyC编译器本地测试脚本"
echo "========================================="

# 检查编译器是否已构建
if [ ! -f "build/compiler" ] && [ ! -f "build/compiler.exe" ]; then
    echo "错误: 编译器未构建，请先运行: cmake -S . -B build && cmake --build build"
    exit 1
fi

# 确定编译器路径
if [ -f "build/compiler.exe" ]; then
    COMPILER="./build/compiler.exe"
else
    COMPILER="./build/compiler"
fi

echo "编译器路径: $COMPILER"
echo ""

# 创建测试目录
mkdir -p test_output

# 测试1: 最简单的返回常量
echo "测试1: return常量"
cat > test_output/test1.tc << 'EOF'
int main() {
    return 42;
}
EOF

$COMPILER < test_output/test1.tc > test_output/test1.s
echo "生成的汇编文件: test_output/test1.s"
echo "---前20行---"
head -20 test_output/test1.s
echo ""

# 测试2: 算术表达式
echo "测试2: 算术表达式"
cat > test_output/test2.tc << 'EOF'
int main() {
    return 2 + 3 * 4 - 5;
}
EOF

$COMPILER < test_output/test2.tc > test_output/test2.s
echo "生成完成: test_output/test2.s"
echo ""

# 测试3: 全局变量和函数调用
echo "测试3: 全局变量和函数调用"
cat > test_output/test3.tc << 'EOF'
const int base = 10;
int global = 5;

int add(int a, int b) {
    return a + b;
}

int main() {
    int x = add(global, base);
    return x;
}
EOF

$COMPILER < test_output/test3.tc > test_output/test3.s
echo "生成完成: test_output/test3.s"
echo ""

# 测试4: if-else语句
echo "测试4: if-else语句"
cat > test_output/test4.tc << 'EOF'
int main() {
    int x = 10;
    if (x > 5) {
        x = 20;
    } else {
        x = 30;
    }
    return x;
}
EOF

$COMPILER < test_output/test4.tc > test_output/test4.s
echo "生成完成: test_output/test4.s"
echo ""

# 测试5: while循环
echo "测试5: while循环"
cat > test_output/test5.tc << 'EOF'
int main() {
    int i = 0;
    int sum = 0;
    while (i < 10) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
EOF

$COMPILER < test_output/test5.tc > test_output/test5.s
echo "生成完成: test_output/test5.s"
echo ""

# 测试6: break和continue
echo "测试6: break和continue"
cat > test_output/test6.tc << 'EOF'
int main() {
    int i = 0;
    int sum = 0;
    while (i < 100) {
        i = i + 1;
        if (i == 5) {
            continue;
        }
        if (i == 10) {
            break;
        }
        sum = sum + 1;
    }
    return sum;
}
EOF

$COMPILER < test_output/test6.tc > test_output/test6.s
echo "生成完成: test_output/test6.s"
echo ""

# 测试7: 优化模式测试
echo "测试7: 优化模式测试"
cat > test_output/test7.tc << 'EOF'
int main() {
    const int a = 2 + 3;
    const int b = a * 4;
    return b;
}
EOF

echo "不启用优化:"
$COMPILER < test_output/test7.tc > test_output/test7_no_opt.s
wc -l test_output/test7_no_opt.s | awk '{print "汇编行数: " $1}'

echo "启用优化 (-opt):"
$COMPILER -opt < test_output/test7.tc > test_output/test7_opt.s
wc -l test_output/test7_opt.s | awk '{print "汇编行数: " $1}'
echo ""

# 测试8: 逻辑运算（短路求值）
echo "测试8: 逻辑运算"
cat > test_output/test8.tc << 'EOF'
int main() {
    int x = 1;
    int y = 0;
    if (x > 0 || y > 0) {
        x = 100;
    }
    if (x > 0 && y == 0) {
        x = x + 1;
    }
    return x;
}
EOF

$COMPILER < test_output/test8.tc > test_output/test8.s
echo "生成完成: test_output/test8.s"
echo ""

# 测试9: 递归函数
echo "测试9: 递归函数"
cat > test_output/test9.tc << 'EOF'
int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    return fib(10);
}
EOF

$COMPILER < test_output/test9.tc > test_output/test9.s
echo "生成完成: test_output/test9.s"
echo ""

echo "========================================="
echo "基本测试完成！"
echo "========================================="
echo ""
echo "下一步："
echo "1. 使用RISC-V模拟器运行汇编程序（需要安装riscv32-unknown-elf-gcc或spike）"
echo "2. 与gcc进行差分测试对比"
echo "3. 运行项目的单元测试: cd build && ctest"
echo ""
echo "测试输出文件位于: test_output/"

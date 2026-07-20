# 本地测试脚本 - 用于验证编译器是否符合测评要求

Write-Host "=========================================" -ForegroundColor Green
Write-Host "ToyC编译器本地测试脚本 (Windows版)" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
Write-Host ""

# 检查编译器是否已构建
$compilerPath = $null
if (Test-Path "build\compiler.exe") {
    $compilerPath = ".\build\compiler.exe"
} elseif (Test-Path "build\compiler") {
    $compilerPath = ".\build\compiler"
} else {
    Write-Host "错误: 编译器未构建" -ForegroundColor Red
    Write-Host "请先运行:" -ForegroundColor Yellow
    Write-Host "  cmake -S . -B build" -ForegroundColor Yellow
    Write-Host "  cmake --build build" -ForegroundColor Yellow
    exit 1
}

Write-Host "编译器路径: $compilerPath" -ForegroundColor Cyan
Write-Host ""

# 创建测试目录
New-Item -ItemType Directory -Force -Path "test_output" | Out-Null

# 测试1: 最简单的返回常量
Write-Host "测试1: return常量" -ForegroundColor Yellow
@"
int main() {
    return 42;
}
"@ | Out-File -FilePath "test_output\test1.tc" -Encoding ASCII

Get-Content "test_output\test1.tc" | & $compilerPath | Out-File -FilePath "test_output\test1.s" -Encoding ASCII
Write-Host "生成的汇编文件: test_output\test1.s" -ForegroundColor Green
Write-Host "---前20行---" -ForegroundColor Cyan
Get-Content "test_output\test1.s" -Head 20
Write-Host ""

# 测试2: 算术表达式
Write-Host "测试2: 算术表达式" -ForegroundColor Yellow
@"
int main() {
    return 2 + 3 * 4 - 5;
}
"@ | Out-File -FilePath "test_output\test2.tc" -Encoding ASCII

Get-Content "test_output\test2.tc" | & $compilerPath | Out-File -FilePath "test_output\test2.s" -Encoding ASCII
Write-Host "生成完成: test_output\test2.s" -ForegroundColor Green
Write-Host ""

# 测试3: 全局变量和函数调用
Write-Host "测试3: 全局变量和函数调用" -ForegroundColor Yellow
@"
const int base = 10;
int global = 5;

int add(int a, int b) {
    return a + b;
}

int main() {
    int x = add(global, base);
    return x;
}
"@ | Out-File -FilePath "test_output\test3.tc" -Encoding ASCII

Get-Content "test_output\test3.tc" | & $compilerPath | Out-File -FilePath "test_output\test3.s" -Encoding ASCII
Write-Host "生成完成: test_output\test3.s" -ForegroundColor Green
Write-Host ""

# 测试4: if-else语句
Write-Host "测试4: if-else语句" -ForegroundColor Yellow
@"
int main() {
    int x = 10;
    if (x > 5) {
        x = 20;
    } else {
        x = 30;
    }
    return x;
}
"@ | Out-File -FilePath "test_output\test4.tc" -Encoding ASCII

Get-Content "test_output\test4.tc" | & $compilerPath | Out-File -FilePath "test_output\test4.s" -Encoding ASCII
Write-Host "生成完成: test_output\test4.s" -ForegroundColor Green
Write-Host ""

# 测试5: while循环
Write-Host "测试5: while循环" -ForegroundColor Yellow
@"
int main() {
    int i = 0;
    int sum = 0;
    while (i < 10) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
"@ | Out-File -FilePath "test_output\test5.tc" -Encoding ASCII

Get-Content "test_output\test5.tc" | & $compilerPath | Out-File -FilePath "test_output\test5.s" -Encoding ASCII
Write-Host "生成完成: test_output\test5.s" -ForegroundColor Green
Write-Host ""

# 测试6: break和continue
Write-Host "测试6: break和continue" -ForegroundColor Yellow
@"
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
"@ | Out-File -FilePath "test_output\test6.tc" -Encoding ASCII

Get-Content "test_output\test6.tc" | & $compilerPath | Out-File -FilePath "test_output\test6.s" -Encoding ASCII
Write-Host "生成完成: test_output\test6.s" -ForegroundColor Green
Write-Host ""

# 测试7: 优化模式测试
Write-Host "测试7: 优化模式测试" -ForegroundColor Yellow
@"
int main() {
    const int a = 2 + 3;
    const int b = a * 4;
    return b;
}
"@ | Out-File -FilePath "test_output\test7.tc" -Encoding ASCII

Write-Host "不启用优化:" -ForegroundColor Cyan
Get-Content "test_output\test7.tc" | & $compilerPath | Out-File -FilePath "test_output\test7_no_opt.s" -Encoding ASCII
$lines = (Get-Content "test_output\test7_no_opt.s").Count
Write-Host "汇编行数: $lines" -ForegroundColor Green

Write-Host "启用优化 (-opt):" -ForegroundColor Cyan
Get-Content "test_output\test7.tc" | & $compilerPath -opt | Out-File -FilePath "test_output\test7_opt.s" -Encoding ASCII
$linesOpt = (Get-Content "test_output\test7_opt.s").Count
Write-Host "汇编行数: $linesOpt" -ForegroundColor Green
Write-Host ""

# 测试8: 逻辑运算（短路求值）
Write-Host "测试8: 逻辑运算" -ForegroundColor Yellow
@"
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
"@ | Out-File -FilePath "test_output\test8.tc" -Encoding ASCII

Get-Content "test_output\test8.tc" | & $compilerPath | Out-File -FilePath "test_output\test8.s" -Encoding ASCII
Write-Host "生成完成: test_output\test8.s" -ForegroundColor Green
Write-Host ""

# 测试9: 递归函数
Write-Host "测试9: 递归函数" -ForegroundColor Yellow
@"
int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    return fib(10);
}
"@ | Out-File -FilePath "test_output\test9.tc" -Encoding ASCII

Get-Content "test_output\test9.tc" | & $compilerPath | Out-File -FilePath "test_output\test9.s" -Encoding ASCII
Write-Host "生成完成: test_output\test9.s" -ForegroundColor Green
Write-Host ""

Write-Host "=========================================" -ForegroundColor Green
Write-Host "基本测试完成！" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
Write-Host ""
Write-Host "下一步：" -ForegroundColor Yellow
Write-Host "1. 使用RISC-V模拟器运行汇编程序" -ForegroundColor White
Write-Host "2. 与gcc进行差分测试对比" -ForegroundColor White
Write-Host "3. 运行项目的单元测试: cd build ; ctest" -ForegroundColor White
Write-Host ""
Write-Host "测试输出文件位于: test_output\" -ForegroundColor Cyan

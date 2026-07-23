#!/bin/bash

BENCHMARK_DIR="$(cd "$(dirname "$0")" && pwd)"
COMPILER_BUILD="${BENCHMARK_DIR}/../../build/compiler"
RESULTS_DIR="${BENCHMARK_DIR}/results"

mkdir -p "$RESULTS_DIR"

echo "ToyC Compiler Performance Benchmark"
echo "===================================="
echo ""

TESTS=(
    "fibonacci"
    "loop_sum"
    "nested_loops"
    "global_vars"
)

OPT_LEVELS=("-O0" "-O1" "-O2")

for test in "${TESTS[@]}"; do
    echo "Test: $test"
    echo "------"
    
    TC_FILE="${BENCHMARK_DIR}/${test}.tc"
    
    for opt in "${OPT_LEVELS[@]}"; do
        echo "  Optimization: $opt"
        
        if [ "$opt" == "-O0" ]; then
            OPT_FLAG=""
        else
            OPT_FLAG="$opt"
        fi
        
        S_FILE="${RESULTS_DIR}/${test}${opt}.s"
        
        if [ -f "$COMPILER_BUILD" ]; then
            "$COMPILER_BUILD" $OPT_FLAG < "$TC_FILE" > "$S_FILE" 2>&1
            
            if [ $? -eq 0 ]; then
                LINES=$(wc -l < "$S_FILE")
                LW_SW=$(grep -E '\b(lw|sw)\b' "$S_FILE" | wc -l)
                ARITH=$(grep -E '\b(add|sub|mul|div|rem|and|or|slt)\b' "$S_FILE" | wc -l)
                
                echo "    Generated: $LINES lines"
                echo "    Load/Store: $LW_SW instructions"
                echo "    Arithmetic: $ARITH instructions"
            else
                echo "    Compilation failed"
            fi
        else
            echo "    Compiler not found at $COMPILER_BUILD"
        fi
        echo ""
    done
    
    echo ""
done

echo "Benchmark complete."

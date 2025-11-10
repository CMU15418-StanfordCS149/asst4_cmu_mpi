#!/bin/bash

# 验证所有测试样例的布线图和 cost 矩阵是否一致
# 使用方法: ./validate_all.sh

# 测试名称数组
test_names=("easy" "medium" "hard" "extreme")
# 进程数数组
test_nprocs=(1 2 4 8)

echo "=================================="
echo "开始验证所有测试样例"
echo "=================================="
echo ""

total_tests=0
passed_tests=0
failed_tests=0

# 遍历所有测试组合
for test in "${test_names[@]}"; do
    for nproc in "${test_nprocs[@]}"; do
        total_tests=$((total_tests + 1))
        
        # 构造文件路径
        wires_file="./inputs/timeinput/${test}_4096_wires_${nproc}.txt"
        occupancy_file="./inputs/timeinput/${test}_4096_occupancy_${nproc}.txt"
        
        echo "测试 [$total_tests/16]: ${test} with ${nproc} cores"
        echo "  布线文件: ${wires_file}"
        echo "  Cost文件: ${occupancy_file}"
        
        # 检查文件是否存在
        if [ ! -f "$wires_file" ]; then
            echo "  ❌ 错误: 布线文件不存在"
            failed_tests=$((failed_tests + 1))
            echo ""
            continue
        fi
        
        if [ ! -f "$occupancy_file" ]; then
            echo "  ❌ 错误: Cost文件不存在"
            failed_tests=$((failed_tests + 1))
            echo ""
            continue
        fi
        
        # 运行验证脚本并捕获输出
        output=$(python3 validate.py -r "$wires_file" -c "$occupancy_file" 2>&1)
        
        # 检查输出中是否包含 ERROR
        if echo "$output" | grep -qi "ERROR"; then
            echo "  ❌ 验证失败"
            echo "  详细错误信息:"
            echo "$output" | sed 's/^/    /'
            failed_tests=$((failed_tests + 1))
        else
            echo "  ✓ 验证通过"
            passed_tests=$((passed_tests + 1))
        fi
        
        echo ""
    done
done

echo "=================================="
echo "验证结果统计"
echo "=================================="
echo "总测试数: $total_tests"
echo "通过: $passed_tests"
echo "失败: $failed_tests"
echo ""

if [ $failed_tests -eq 0 ]; then
    echo "🎉 所有测试都通过了！"
    exit 0
else
    echo "⚠️  有 $failed_tests 个测试失败"
    exit 1
fi

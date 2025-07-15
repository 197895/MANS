#!/bin/bash

source ./config.sh

echo "=============================="
echo "Running U2 Compression Test..."
echo "=============================="
bash ./bash_module/run_test_thr_u2_cpu.sh

echo ""
echo "=============================="
echo "Running U4 Compression Test..."
echo "=============================="
bash ./bash_module/run_test_thr_u4_cpu.sh

echo ""
echo "=============================="
echo "All Tests Completed."
echo "=============================="
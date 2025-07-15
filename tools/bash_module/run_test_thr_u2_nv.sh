#!/bin/bash

# 配置路径
NVCOMP_PATH=/home/gyd/HWJ/nvcomp/bin
ADM16=~/project/mans/nv/adm/mappingV15_uint16
ANS_cmp=~/project/mans/nv/ans-encoder/build/bin/cudaans_compress
ANS_decmp=~/project/mans/nv/ans-encoder/build/bin/cudaans_decompress
TEST_DIR=~/project/mans/testdata/u2
OUTPUT_DIR=../output

# 运行测试
run() {
    mkdir -p $OUTPUT_DIR
    output_file=$OUTPUT_DIR/U2THR-nv.txt
    echo "START" > $output_file

    for dir in `ls $TEST_DIR`
    do
        if [[ -d $TEST_DIR"/"$dir ]]; then
            echo "Processing Directory: $dir"
            echo "DIR: $dir" >> $output_file

            for file in `ls $TEST_DIR"/"$dir`
            do
                if [[ $file == *".u2" ]]; then
                    echo "   FILE: $file"
                    echo "FILE: $file" >> $output_file

                    file_path="$TEST_DIR/$dir/$file"
                    file_size=$(stat -c%s "$file_path")
                    file_KB=$(echo "$file_size / 1024" | bc)
                    echo "SIZE: $file_KB KB" >> $output_file

                    
                    # Step 1: huffman
                    gdeflate_output=$($NVCOMP_PATH/benchmark_gdeflate_chunked -a 0 -f "$file_path")
                    huffman_cmp_thr=$(echo "$gdeflate_output" | grep "compression throughput" | awk 'NR==1 {print $4}')
                    huffman_decmp_thr=$(echo "$gdeflate_output" | grep "decompression throughput" | awk '{print $4}')
                    echo "  NV-Huffman Compressed Thr: $huffman_cmp_thr" >> $output_file
                    echo "  NV-Huffman Decompressed Thr: $huffman_decmp_thr" >> $output_file

                    # Step 2: ans
                    ans_output=$($NVCOMP_PATH/benchmark_ans_chunked -f "$file_path")
                    ans_cmp_thr=$(echo "$ans_output" | grep "compression throughput" | awk 'NR==1 {print $4}')
                    ans_decmp_thr=$(echo "$ans_output" | grep "decompression throughput" | awk '{print $4}')
                    echo "  NV-ANS Compressed Thr: $ans_cmp_thr" >> $output_file
                    echo "  NV-ANS Decompressed Thr: $ans_decmp_thr" >> $output_file
                    
                    # Step 3: ADM
                    adm_output_path="$file_path.adm"
                    $ADM16 "$file_path" "$adm_output_path"
                    adm_output=$($ADM16 "$file_path" "$adm_output_path")
                    adm_cmp_time=$(echo "$adm_output" | grep "Total Cmp Time: " | awk 'NR==1 {print $4}')
                    adm_decmp_time=$(echo "$adm_output" | grep "Total Decmp Time:" | awk '{print $4}')
                    ans_output=$($ANS_cmp "$adm_output_path" "$adm_output_path.tmp")
                    ans_cmp_time=$(echo "$ans_output" | grep "comp   time" | awk 'NR==1 {print $3}')
                    ans_output=$($ANS_decmp "$adm_output_path.tmp" "$adm_output_path.out")
                    ans_decmp_time=$(echo "$ans_output" | grep "decomp time" | awk 'NR==1 {print $3}')
                    mans_cmp_thr=$(echo "scale=9; $file_size / 1024 / 1024 / 1024/ (($adm_cmp_time + $ans_cmp_time) / 1000)" | bc)
                    mans_decmp_thr=$(echo "scale=9; $file_size / 1024 / 1024 / 1024 / (($adm_decmp_time + $ans_decmp_time) / 1000)" | bc)
                    echo "  MANS Compressed Thr: $mans_cmp_thr" >> $output_file
                    echo "  MANS Decompressed Thr: $mans_decmp_thr" >> $output_file


                    # 清理临时文件
                    rm -f  "$adm_output_path" "$adm_output_path.tmp" "$adm_output_path.out"                    
                fi
            done
            echo "" >> $output_file
        fi
    done

    echo "FINISHED." >> $output_file
}

# 运行测试
run

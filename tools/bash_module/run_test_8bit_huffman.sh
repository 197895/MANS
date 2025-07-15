#!/bin/bash

# 配置路径
GAPARRAY_DIR=/home/gyd/HWJ/ipdps22-opthuffdec-main/orig-gap-array/encoder/bin
GPUHD_DIR=/home/gyd/HWJ/ipdps22-opthuffdec-main/orig-self-sync/bin
TEST_DIR=/home/gyd/HWJ/data/mans-data
filesize_set_KBs="262144 65536 16384 4096 1024 256 64 16 4 1"
OUTPUT_DIR=./output

# 运行测试
run() {
    mkdir -p $OUTPUT_DIR
    output_file=$OUTPUT_DIR/compression_results-gpuhd-gaparray.txt
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
                    file_size_original=$(stat -c%s "$file_path")

                    for filesize_set_KB in $filesize_set_KBs
                    do
                        filesize_set=$((filesize_set_KB * 1024))

                        if [[ $file_size_original -lt $filesize_set ]]; then
                            echo "  Skipping size: $filesize_set, larger than file"
                            continue
                        fi

                        # 生成分片
                        split_dir="./split_files"
                        mkdir -p $split_dir
                        split -b $filesize_set "$file_path" "$split_dir/piece"

                        total_original_size=0
                        total_cr_gpu=0
                        total_cr_gap=0
                        piece_count=0

                        for piece in `ls $split_dir`
                        do
                            if [[ $piece_count -ge 10 ]]; then
                                break
                            fi


                            piece_path="$split_dir/$piece"
                            piece_size=$(stat -c%s "$piece_path")
                            total_original_size=$((total_original_size + piece_size))

                            # Step 1: 运行 gpuhd 压缩
                            # gpu_output=$($GPUHD_DIR/demo 0 "$piece_path" "$piece_size")
                            # cr_gpu=$(echo "$gpu_output" | grep "^CR:" | awk '{print $2}')
                            # total_cr_gpu=$(echo "$total_cr_gpu + $cr_gpu" | bc)
                            # echo "$gpu_output" >> $output_file

                            # Step 2: 运行 gap array 压缩
                            gap_out="$piece_path.gap"
                            gap_output=$($GAPARRAY_DIR/encoder "$piece_path" "$gap_out")
                            gap_ratio=$(echo "$gap_output" | grep "ratio=" | awk -F',' '{print $2}')
                            cr_gap=$(echo "scale=6; 1 / $gap_ratio" | bc -l)
                            total_cr_gap=$(echo "$total_cr_gap + $cr_gap" | bc)

                            rm -f "$gap_out"
                            piece_count=$((piece_count + 1))

                        done

                        # 计算平均压缩比
                        # avg_cr_gpu=$(echo "scale=4; $total_cr_gpu / $piece_count" | bc)
                        avg_cr_gap=$(echo "scale=4; $total_cr_gap / $piece_count" | bc)

                        # 记录结果
                        echo "SIZE: ${filesize_set_KB} KB" >> $output_file
                        # echo "  GPUHD Avg Compression Ratio: $avg_cr_gpu" >> $output_file
                        echo "  GAP Array Avg Compression Ratio: $avg_cr_gap" >> $output_file
                        echo "" >> $output_file

                        # 删除分片目录
                        rm -rf $split_dir
                    done
                fi
            done
        fi
    done

    echo "FINISHED." >> $output_file
}

# 运行测试
run

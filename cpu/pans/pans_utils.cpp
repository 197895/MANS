// pans_utils.cpp
#include "pans_utils.h"
#include "CpuANSEncode.h"
#include "CpuANSDecode.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <cstdlib>

using namespace cpu_ans;

void compressFileWithANS(
    const std::string& inputFilePath,
    const std::string& tempFilePath,
    uint32_t& batchSize,
    uint32_t& compressedSize,
    int precision
    ) {
    std::ifstream inputFile(inputFilePath, std::ios::binary | std::ios::ate);
    if (!inputFile.is_open()) {
        std::cerr << "Error: Could not open input file " << inputFilePath << std::endl;
        return;
    }
    std::streamsize fileSize = inputFile.tellg();
    std::vector<uint8_t> fileData(fileSize);
    inputFile.seekg(0, std::ios::beg);
    inputFile.read(reinterpret_cast<char*>(fileData.data()), fileSize);
    inputFile.close();

    uint8_t* inPtrs = fileData.data();

    batchSize = fileSize;

    uint32_t* outCompressedSize = (uint32_t*)malloc(sizeof(uint32_t));
    uint8_t* encPtrs = (uint8_t*)malloc(getMaxCompressedSize(fileSize));
    ANSCoalescedHeader* headerOut = (ANSCoalescedHeader*)encPtrs;
    uint32_t maxNumCompressedBlocks;

    uint32_t maxUncompressedWords = fileSize / sizeof(ANSDecodedT);
    maxNumCompressedBlocks =
        (maxUncompressedWords + kDefaultBlockSize - 1) / kDefaultBlockSize;
    
    uint4* table = (uint4*)malloc(sizeof(uint4) * kNumSymbols);
    uint32_t* tempHistogram = (uint32_t*)malloc(sizeof(uint32_t) * kNumSymbols);
    uint32_t uncoalescedBlockStride = getMaxBlockSizeUnCoalesced(kDefaultBlockSize);
    uint8_t* compressedBlocks_host = (uint8_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint8_t) * maxNumCompressedBlocks * uncoalescedBlockStride);
    uint32_t* compressedWords_host = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * maxNumCompressedBlocks);
    uint32_t* compressedWords_host_prefix = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * maxNumCompressedBlocks);
    uint32_t* compressedWordsPrefix_host = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * maxNumCompressedBlocks);
    
    std::cout<<"encode start!"<<std::endl;
    double comp_time = 999999;
    
    // Warmup & Benchmark loop
    for(int i = 0; i < 11; i ++){
        auto start = std::chrono::high_resolution_clock::now();  

        ansEncode(
            table,
            tempHistogram,
            precision,
            inPtrs,
            batchSize,
            encPtrs,
            outCompressedSize,
            headerOut,
            maxNumCompressedBlocks,
            uncoalescedBlockStride,
            compressedBlocks_host,
            compressedWords_host,
            compressedWords_host_prefix,
            compressedWordsPrefix_host);

        auto end = std::chrono::high_resolution_clock::now();
        double dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1e3;
        if(comp_time > dur) comp_time = dur;  
    }

    double c_bw = ( 1.0 * fileSize / 1e6 ) / ( (comp_time) * 1e-3 );  
    std::cout << "comp   time " << std::fixed << std::setprecision(3) << comp_time << " ms B/W "   
                  << std::fixed << std::setprecision(1) << c_bw << " MB/s " << std::endl;

    std::ofstream outputFile(tempFilePath, std::ios::binary);
    if (!outputFile.is_open()) {
        std::cerr << "Error: Could not open output file " << tempFilePath << std::endl;
        // Clean up memory before returning
        free(outCompressedSize); free(encPtrs); free(table); free(tempHistogram);
        free(compressedBlocks_host); free(compressedWords_host);
        free(compressedWords_host_prefix); free(compressedWordsPrefix_host);
        return;
    }

    auto blockWordsOut = headerOut->getBlockWords(maxNumCompressedBlocks);
    auto BlockDataStart = headerOut->getBlockDataStart(maxNumCompressedBlocks);
    
    int i = 0;
    for(; i < maxNumCompressedBlocks - 1; i ++){
        auto uncoalescedBlock = compressedBlocks_host + i * uncoalescedBlockStride;
        for(int j = 0; j < kWarpSize; ++j){
            auto warpStateOut = (ANSWarpState*)uncoalescedBlock;
            headerOut->getWarpStates()[i].warpState[j] = (warpStateOut->warpState[j]);
        }
        blockWordsOut[i] = uint2{
            (kDefaultBlockSize << 16) | compressedWords_host[i], 
            compressedWordsPrefix_host[i]};
    }

    // Process last block
    auto uncoalescedBlock = compressedBlocks_host + i * uncoalescedBlockStride;
    for(int j = 0; j < kWarpSize; ++j){
        auto warpStateOut = (ANSWarpState*)uncoalescedBlock;
        headerOut->getWarpStates()[i].warpState[j] = (warpStateOut->warpState[j]);
    }
    
    uint32_t lastBlockWords = fileSize % kDefaultBlockSize;
    lastBlockWords = lastBlockWords == 0 ? kDefaultBlockSize : lastBlockWords;

    blockWordsOut[i] = uint2{
        (lastBlockWords << 16) | compressedWords_host[i], compressedWordsPrefix_host[i]};
    
    outputFile.write(reinterpret_cast<const char*>(encPtrs), headerOut->getCompressedOverhead(maxNumCompressedBlocks));

    i = 0;
    for(; i < maxNumCompressedBlocks - 1; i ++){
        auto uncoalescedBlock = compressedBlocks_host + i * uncoalescedBlockStride;
        uint32_t numWords = compressedWords_host[i];
        uint32_t limitEnd = divUp(numWords, kBlockAlignment / sizeof(ANSEncodedT));

        auto inT = (const uint4*)(uncoalescedBlock + sizeof(ANSWarpState));
        // auto outT = (uint4*)(BlockDataStart + compressedWordsPrefix_host[i]);
        outputFile.write(reinterpret_cast<const char*>(inT), limitEnd << 4);
    }

    // Write last block data
    uint32_t numWords = compressedWords_host[i];
    uint32_t limitEnd = divUp(numWords, kBlockAlignment / sizeof(ANSEncodedT));
    auto inT = (const uint4*)(uncoalescedBlock + sizeof(ANSWarpState));
    outputFile.write(reinterpret_cast<const char*>(inT), limitEnd << 4);

    uint32_t outsize = *outCompressedSize;
    compressedSize = outsize;

    outputFile.close();
    
    // Clean up
    free(outCompressedSize);
    free(encPtrs);
    free(table);
    free(tempHistogram);
    free(compressedBlocks_host);
    free(compressedWords_host);
    free(compressedWords_host_prefix);
    free(compressedWordsPrefix_host);
}

void decompressFileWithANS(
        const std::string& tempFilePath,
        const std::string& outputFilePath, 
        int precision) {
    std::ifstream inFile0(tempFilePath, std::ios::binary);
    if (!inFile0.is_open()) {
         std::cerr << "Error: Could not open input file " << tempFilePath << std::endl;
         return;
    }
    std::vector<uint8_t> fileCompressedHead(32);
    inFile0.read(reinterpret_cast<char*>(fileCompressedHead.data()), 32);
    auto Header = (ANSCoalescedHeader*)fileCompressedHead.data();
    int totalCompressedSize = Header->getTotalCompressedSize();
    int batchSize = Header->getTotalUncompressedWords();
    inFile0.close();

    std::ifstream inFile1(tempFilePath, std::ios::binary);
    std::vector<uint8_t> fileCompressedData(totalCompressedSize);
    inFile1.read(reinterpret_cast<char*>(fileCompressedData.data()), totalCompressedSize);
    inFile1.close();

    uint8_t* decPtrs = (uint8_t*)malloc(sizeof(uint8_t)*(batchSize));
    uint32_t* symbol = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * (1 << precision));
    uint32_t* pdf = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * (1 << precision));
    uint32_t* cdf = (uint32_t*)std::aligned_alloc(kBlockAlignment, sizeof(uint32_t) * (1 << precision));
    
    std::cout<<"decode start!"<<std::endl;
    double decomp_time = 999999;

    // Warmup & Benchmark loop
    for(int i = 0; i < 11; i ++){
        auto start = std::chrono::high_resolution_clock::now();
        ansDecode(
            symbol,
            pdf,
            cdf,
            precision,
            fileCompressedData.data(),
            decPtrs);
        auto end = std::chrono::high_resolution_clock::now();  
        double dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1e3;
        if(decomp_time > dur) decomp_time = dur; 
    }

    double dc_bw = ( 1.0 * totalCompressedSize / 1e6 ) / ( (decomp_time) * 1e-3 );
    std::cout << "decomp time " << std::fixed << std::setprecision(6) << (decomp_time) << " ms B/W "   
                  << std::fixed << std::setprecision(1) << dc_bw << " MB/s" << std::endl;
    
    std::ofstream outFile(outputFilePath, std::ios::binary);
    if (outFile.is_open()) {
        outFile.write(reinterpret_cast<const char*>(decPtrs), batchSize*sizeof(uint8_t));
        outFile.close();
    } else {
        std::cerr << "Error: Could not open output file " << outputFilePath << std::endl;
    }

    free(decPtrs);
    free(symbol);
    free(pdf);
    free(cdf);
    decPtrs = NULL;
}
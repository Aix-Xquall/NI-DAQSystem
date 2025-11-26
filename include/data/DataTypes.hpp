#pragma once

#include <vector>
#include <string>
#include <chrono>

namespace Data
{

    // 定義一塊原始數據 (Raw Data Chunk)
    struct RawDataChunk
    {
        std::string deviceName;   // 來源裝置
        std::vector<double> data; // 採樣數據 (平面化陣列，如果是多通道需自行切割)
        int channelCount;         // 通道數
        int samplesPerChannel;    // 每個通道讀取了多少點
        double sampleRate;        // 取樣率

        // 時間戳記 (使用高解析度時鐘)
        std::chrono::system_clock::time_point timestamp;
    };

}
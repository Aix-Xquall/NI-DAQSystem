#include "utils/CsvFormatter.hpp"
#include <sstream>
#include <iomanip>
#include <cmath> // for floor

namespace Utils {

    std::string CsvFormatter::toCsv(const Data::RawDataChunk& chunk, size_t maxPoints) {
        std::stringstream ss;
        
        // 時間戳記
        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(chunk.timestamp);
        auto epoch = now.time_since_epoch().count();

        // 1. 計算原始數據中有多少組「完整掃描 (Scans)」
        // chunk.data.size() 是所有通道數據的總和
        // numScans 是時間軸上的點數
        size_t numChannels = chunk.channelCount;
        if (numChannels == 0) return ""; // 防呆

        size_t totalScans = chunk.data.size() / numChannels;

        // 2. 決定要輸出的 Scan 數量 (限制在 maxPoints 以內)
        size_t scansToSend = std::min(totalScans, maxPoints);
        
        // 3. 計算步長 (Step)，實現均勻降頻
        // 例如：有 5000 點，要取 20 點，步長 = 250
        double step = 1.0;
        if (scansToSend > 1) {
            step = (double)(totalScans - 1) / (scansToSend - 1);
        }

        // Header: DeviceName, Timestamp, SampleRate, ChannelCount, TotalPoints(Payload)
        // 這裡 TotalPoints 指的是我們即將發送的數據量 (scansToSend * numChannels)
        ss << chunk.deviceName << "," 
           << epoch << "," 
           << chunk.sampleRate << ","
           << numChannels << "," 
           << (scansToSend * numChannels);

        ss << std::fixed << std::setprecision(4);

        // 4. 均勻抽取數據
        for (size_t i = 0; i < scansToSend; ++i) {
            // 計算目前要取的 Scan 索引 (0, step, 2*step...)
            size_t currentScanIdx = (size_t)(i * step);
            
            // 防呆：確保不越界
            if (currentScanIdx >= totalScans) currentScanIdx = totalScans - 1;

            // 計算該 Scan 在平面陣列中的起始位置
            size_t dataStartIndex = currentScanIdx * numChannels;

            // 5. 複製該 Scan 的所有通道數據 (Ch0, Ch1, Ch2...)
            // 這樣確保同一個時間點的所有通道數據都被送出
            for (size_t ch = 0; ch < numChannels; ++ch) {
                ss << "," << chunk.data[dataStartIndex + ch];
            }
        }
        
        return ss.str();
    }
}
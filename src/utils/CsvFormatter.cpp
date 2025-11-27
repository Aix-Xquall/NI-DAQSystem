#include "utils/CsvFormatter.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace Utils
{

    std::string CsvFormatter::toCsv(const Data::RawDataChunk &chunk, size_t maxPoints)
    {
        std::stringstream ss;

        // 時間戳記
        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(chunk.timestamp);
        auto epoch = now.time_since_epoch().count();

        // [修改處] 新增 chunk.channelCount 欄位
        // Header: DeviceName, Timestamp, SampleRate, ChannelCount, TotalPoints
        ss << chunk.deviceName << ","
           << epoch << ","
           << chunk.sampleRate << ","
           << chunk.channelCount << "," // <--- 新增這行
           << chunk.data.size();

        // 限制傳送點數 (UI 顯示用)
        // 注意：如果是多通道，maxPoints 最好是 channelCount 的倍數，以免切到一半
        // 這裡做個簡單處理：確保傳送的點數能被通道數整除
        size_t effectiveMax = maxPoints;
        if (chunk.channelCount > 0)
        {
            effectiveMax = (maxPoints / chunk.channelCount) * chunk.channelCount;
        }
        if (effectiveMax == 0)
            effectiveMax = chunk.channelCount; // 至少傳一組

        size_t pointsToSend = std::min(chunk.data.size(), effectiveMax);

        ss << std::fixed << std::setprecision(4);

        for (size_t i = 0; i < pointsToSend; ++i)
        {
            ss << "," << chunk.data[i];
        }

        return ss.str();
    }

}
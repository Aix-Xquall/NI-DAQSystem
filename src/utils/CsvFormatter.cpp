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

        // 為了確保 UI 繪圖時數據是對齊的，我們確保傳送的點數是 channelCount 的倍數
        // 例如：若有 3 通道，我們不想只傳 4 個點 (ch0, ch1, ch2, ch0)，這樣第二組數據不完整
        size_t safeMaxPoints = (maxPoints / chunk.channelCount) * chunk.channelCount;
        if (safeMaxPoints == 0 && maxPoints > 0)
            safeMaxPoints = chunk.channelCount; // 至少傳一組

        size_t pointsToSend = std::min(chunk.data.size(), safeMaxPoints);

        ss << std::fixed << std::setprecision(4);

        for (size_t i = 0; i < pointsToSend; ++i)
        {
            ss << "," << chunk.data[i];
        }

        return ss.str();
    }

}
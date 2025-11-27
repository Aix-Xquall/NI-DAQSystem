#pragma once

#include <string>
#include "data/DataTypes.hpp"

namespace Utils
{

    class CsvFormatter
    {
    public:
        // 靜態方法：將 RawDataChunk 轉為 CSV 字串
        // maxPoints: 限制輸出點數 (避免 UDP 爆掉)，預設 50 點
        static std::string toCsv(const Data::RawDataChunk &chunk, size_t maxPoints = 50);
    };

}
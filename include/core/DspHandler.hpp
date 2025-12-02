#pragma once
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <deque>
#include "utils/ConfigLoader.hpp"
#include "data/DataTypes.hpp"
#include "dsp/Downsampler.hpp"

namespace Core
{

    /**
     * @brief Slot 通道狀態管理
     * 包含該通道的降頻器以及 "Sample and Hold" 所需的最後數值
     */
    struct ChannelState
    {
        std::string label;                             // 通道標籤 (e.g., "ai0")
        std::unique_ptr<DSP::Downsampler> downsampler; // 專屬降頻器
        double lastValue;                              // S&H: 上一次的有效輸出值
        bool hasValue;                                 // 是否已有初始值
        bool isMaster;                                 // 是否為該 Slot 的主頻率通道 (決定輸出時機)
    };

    /**
     * @brief Slot 處理單元
     * 代表一個實體的 Slot (例如 Slot 8)，包含多個通道
     */
    struct SlotBundle
    {
        std::string slotName;               // 用於 CSV Header (e.g., "Slot_1_NI-9232")
        std::vector<ChannelState> channels; // 該 Slot 底下的所有通道
        int masterChannelIdx;               // 主通道索引 (通常是頻率最高的通道)

        // 暫存輸出的 CSV 字串列表 (等待 UDP 發送)
        std::vector<std::string> pendingPackets;
    };

    class DspHandler
    {
    public:
        // 初始化：依據 SystemConfig 建立 Slot 映射表
        explicit DspHandler(const Utils::SystemConfig &config);

        /**
         * @brief 處理原始數據塊 (核心函式)
         * 1. 解交錯 (De-interleave)
         * 2. 降頻運算
         * 3. Sample & Hold 補值
         * 4. 產生 CSV
         * @param chunk 來自 DAQ 的原始數據
         * @return std::vector<std::string> 準備發送的 CSV 封包列表 (每個 Slot 獨立)
         */
        std::vector<std::string> process(const Data::RawDataChunk &chunk);

    private:
        // 映射表: TaskName -> 該 Task 包含哪些 SlotBundle
        // 一個 Task (如 Task_B) 可能包含多個 Slots (Slot 3,4,5...)
        // 為了快速查找，我們使用 TaskName 作為 Key
        struct TaskMap
        {
            std::vector<int> channelToSlotIdx;              // 原始通道索引 -> 對應 slots 陣列中的索引
            std::vector<int> channelToChIdx;                // 原始通道索引 -> 對應 SlotBundle 內部的 channel 索引
            std::vector<std::shared_ptr<SlotBundle>> slots; // 該 Task 涉及的所有 Slot 物件
        };

        std::map<std::string, TaskMap> m_taskMappings;

        // 儲存所有的 SlotBundle 實體 (擁有權)
        std::vector<std::shared_ptr<SlotBundle>> m_allSlots;
    };
}
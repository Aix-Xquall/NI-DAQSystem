#pragma once
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <deque>
#include "utils/ConfigLoader.hpp"
#include "data/DataTypes.hpp"
#include "dsp/Downsampler.hpp"
#include "dsp/FftBuffer.hpp"
#include "dsp/FftTransformer.hpp"

namespace Core
{
    /**
     * @brief 通道狀態
     * 維護每個通道的降頻、FFT 緩衝與最新數值
     */
    struct ChannelState
    {
        std::string label;                             
        std::unique_ptr<DSP::Downsampler> downsampler; 
        
        // --- 時域相關 ---
        double lastValue;   // Sample & Hold: 儲存降頻後的最新數值
        bool hasValue;      // 標記是否已收到至少一筆數據
        
        // --- FFT 相關 ---
        bool fftActive;
        std::unique_ptr<DSP::FftBuffer> fftBuffer;
        std::unique_ptr<DSP::FftTransformer> fftTransformer;
        
        std::vector<double> lastFftResult; // 儲存最新的 FFT Magnitude
        bool hasNewFft;                    // 標記是否有新的 FFT 計算完成
        
        bool isMaster; // 是否為主通道 (負責觸發 Slot 打包)
    };

    /**
     * @brief Slot 處理單元
     */
    struct SlotBundle
    {
        std::string slotName;               
        std::vector<ChannelState> channels; 
        int masterChannelIdx;
        
        // Slot 層級參數
        double effectiveRate; // MA 後的有效取樣率
    };

    class DspHandler
    {
    public:
        explicit DspHandler(const Utils::SystemConfig &config);
        
        /**
         * @brief 處理原始數據，回傳需要發送的 UDP 封包列表
         * 可能包含 "時域封包" 與 "頻域封包"
         */
        std::vector<std::string> process(const Data::RawDataChunk &chunk);

    private:
        struct TaskMap
        {
            std::vector<int> channelToSlotIdx;              
            std::vector<int> channelToChIdx;                
            std::vector<std::shared_ptr<SlotBundle>> slots; 
        };

        std::map<std::string, TaskMap> m_taskMappings;
        std::vector<std::shared_ptr<SlotBundle>> m_allSlots;
    };
}
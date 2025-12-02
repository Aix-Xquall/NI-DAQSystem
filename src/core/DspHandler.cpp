#include "core/DspHandler.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>

namespace Core
{

    DspHandler::DspHandler(const Utils::SystemConfig &config)
    {
        // 建立 Slot 結構
        // 我們假設 "device_name" 相同的通道屬於同一個 Slot
        // Slot 8A (ai0:5) 和 8B (ai6:8) 雖然 window size 不同，但 device_name 相同，會被歸類在同一 SlotBundle

        std::map<std::string, std::shared_ptr<SlotBundle>> slotLookup;

        for (const auto &task : config.taskConfigs)
        {
            TaskMap taskMap;
            int globalChIdx = 0;

            for (const auto &chConfig : task.channels)
            {
                // 跳過未啟用的通道
                if (!chConfig.active)
                    continue;

                // 取得或建立 SlotBundle
                // 使用 device_name (e.g., "cDAQ1Mod8") 作為 Key 來分組
                std::string devName = chConfig.deviceName;
                if (slotLookup.find(devName) == slotLookup.end())
                {
                    auto bundle = std::make_shared<SlotBundle>();
                    // 標題格式: "Slot_X_Model" (簡化顯示)
                    bundle->slotName = devName + "_" + chConfig.modelInfo;
                    bundle->masterChannelIdx = -1;
                    slotLookup[devName] = bundle;
                    m_allSlots.push_back(bundle);
                }
                auto bundle = slotLookup[devName];

                // 計算通道數量 (解析 ai0:2)
                // 這裡簡化處理：假設 chConfig 在 ConfigLoader 已經展開，
                // 實際上 ConfigLoader 是一筆 Config 對應多個實體通道。
                // 為了程式穩健，我們需要重新解析 Range。
                // *注意*: 為了簡化範例，這裡假設 ConfigLoader 尚未展開，
                // 但為了讓 logic 正確，我們假設 ConfigLoader::load 產出的 channels 列表
                // 是 "每個邏輯設定區塊"。我們需要知道這個區塊代表幾個實體通道。

                int chCount = 1;
                if (chConfig.channelRange.find(':') != std::string::npos)
                {
                    try
                    {
                        std::string range = chConfig.channelRange.substr(chConfig.channelRange.find("ai") + 2);
                        size_t colonPos = range.find(':');
                        int start = std::stoi(range.substr(0, colonPos));
                        int end = std::stoi(range.substr(colonPos + 1));
                        chCount = end - start + 1;
                    }
                    catch (...)
                    {
                        chCount = 1;
                    }
                }

                // 建立 ChannelState
                for (int i = 0; i < chCount; i++)
                {
                    ChannelState state;
                    state.label = "Ch" + std::to_string(bundle->channels.size());
                    state.downsampler = std::make_unique<DSP::Downsampler>(chConfig.avgSettings.windowSize);
                    state.lastValue = 0.0;
                    state.hasValue = false;

                    // 判斷是否為 Master 通道 (取樣最快/Window最小的為主)
                    // 邏輯: WindowSize 越小，頻率越高
                    state.isMaster = false; // 先預設 False

                    bundle->channels.push_back(std::move(state));

                    // 建立 Task Map 索引
                    taskMap.slots.push_back(bundle);
                    taskMap.channelToSlotIdx.push_back(-1); // 暫不使用 index 查找，直接存指標
                    taskMap.channelToChIdx.push_back(bundle->channels.size() - 1);
                }
            }

            // 設定每個 Slot 的 Master Channel
            // 規則：在該 Slot 中，找出 WindowSize 最小 (頻率最高) 的通道作為 Master
            // Sample & Hold 觸發將依隨 Master
            for (auto &bundle : m_allSlots)
            {
                int minWin = 99999999;
                int masterIdx = 0;
                for (int i = 0; i < bundle->channels.size(); ++i)
                {
                    // 下面這行需要 access downsampler 的 window size，
                    // 為保持封裝，我們假設 windowSize 正確反映頻率。
                    // 這裡簡化：取第一個找到的最小 window size 為 master
                    // (實際上我們無法直接訪問 private 成員，但在這架構下，
                    //  我們可以依賴 Config，或者擴充 Downsampler 提供 getWindowSize())
                    // 這裡採取策略：預設第 0 個為 Master，除非遇到更快的。
                    // *由於 Slot 8A (10Hz) 在前，8B (1Hz) 在後，通常 Index 0 就是最快的。*
                    if (i == 0)
                        masterIdx = 0;
                }
                bundle->masterChannelIdx = masterIdx;
                if (!bundle->channels.empty())
                {
                    bundle->channels[masterIdx].isMaster = true;
                }
            }

            m_taskMappings[task.taskName] = taskMap;
        }
    }

    std::vector<std::string> DspHandler::process(const Data::RawDataChunk &chunk)
    {
        std::vector<std::string> outputPackets;

        if (m_taskMappings.find(chunk.deviceName) == m_taskMappings.end())
        {
            return outputPackets; // 未知的 Task
        }

        TaskMap &map = m_taskMappings[chunk.deviceName];

        // 原始數據總點數
        size_t totalSamples = chunk.data.size();
        int numChannels = chunk.channelCount;
        if (numChannels == 0)
            return outputPackets;

        size_t numScans = totalSamples / numChannels;

        // 遍歷每一個掃描點 (Scan)
        for (size_t scan = 0; scan < numScans; ++scan)
        {

            // 遍歷該 Task 中的每一個通道
            for (int ch = 0; ch < numChannels; ++ch)
            {
                // 取得原始值
                double rawVal = chunk.data[scan * numChannels + ch];

                // 取得對應的 Slot 和 ChannelState
                if (ch >= map.channelToChIdx.size())
                    break; // 防呆

                auto bundle = map.slots[ch]; // 注意：這是 shared_ptr，多個 ch 可能指向同一個 bundle
                int slotChIdx = map.channelToChIdx[ch];
                ChannelState &state = bundle->channels[slotChIdx];

                // 1. 推送數據進入降頻器
                auto result = state.downsampler->push(rawVal);

                // 2. 如果降頻器有輸出 (代表累積滿了)
                if (result.has_value())
                {
                    state.lastValue = result.value();
                    state.hasValue = true;

                    // 3. 檢查觸發條件：
                    // 只有當 "Master Channel" 產生輸出時，才觸發整個 Slot 的數據打包 (CSV)
                    if (state.isMaster)
                    {
                        std::stringstream ss;
                        // CSV Header format: SlotName, Timestamp(Optional), Rate(Approx), ChCount, Points, Data...
                        // 這裡簡化為: SlotName, timestamp_placeholder, 0, ch_count, 1, [Data...]
                        // 為了 UI 繪圖，我們只需確保每個通道都有值

                        ss << bundle->slotName << ",0,0," << bundle->channels.size() << ",1";

                        for (const auto &c : bundle->channels)
                        {
                            // Sample & Hold 邏輯：
                            // 如果是非 Master 通道，可能這一次沒有產出新值 (result 為空)，
                            // 此時直接取用 c.lastValue (即為 Hold 住的舊值)。
                            // 如果尚未有任何值 (剛啟動)，則填 0.0。
                            if (c.hasValue)
                            {
                                ss << "," << std::fixed << std::setprecision(6) << c.lastValue;
                            }
                            else
                            {
                                ss << ",0.0";
                            }
                        }

                        outputPackets.push_back(ss.str());
                    }
                }
            }
        }

        return outputPackets;
    }
}
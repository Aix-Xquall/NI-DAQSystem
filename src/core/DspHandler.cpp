#include "core/DspHandler.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>

namespace Core
{

    DspHandler::DspHandler(const Utils::SystemConfig &config)
    {
        std::map<std::string, std::shared_ptr<SlotBundle>> slotLookup;

        for (const auto &task : config.taskConfigs)
        {
            TaskMap taskMap;

            for (const auto &chConfig : task.channels)
            {
                if (!chConfig.active) continue;

                // 1. 取得或建立 SlotBundle
                std::string devName = chConfig.deviceName;
                if (slotLookup.find(devName) == slotLookup.end())
                {
                    auto bundle = std::make_shared<SlotBundle>();
                    bundle->slotName = devName + "_" + chConfig.modelInfo;
                    bundle->masterChannelIdx = -1;
                    
                    // 計算有效取樣率
                    double effRate = task.sampleRate;
                    if (chConfig.avgSettings.active && chConfig.avgSettings.windowSize > 0) {
                        effRate /= chConfig.avgSettings.windowSize;
                    }
                    bundle->effectiveRate = effRate;

                    slotLookup[devName] = bundle;
                    m_allSlots.push_back(bundle);
                }
                auto bundle = slotLookup[devName];

                // 2. 計算實體通道數
                int chCount = 1;
                if (chConfig.channelRange.find(':') != std::string::npos) {
                    try {
                        std::string range = chConfig.channelRange.substr(chConfig.channelRange.find("ai") + 2);
                        size_t colonPos = range.find(':');
                        int start = std::stoi(range.substr(0, colonPos));
                        int end = std::stoi(range.substr(colonPos + 1));
                        chCount = end - start + 1;
                    } catch (...) { chCount = 1; }
                }

                // 3. 建立 ChannelState
                for (int i = 0; i < chCount; i++)
                {
                    ChannelState state;
                    state.label = "Ch" + std::to_string(bundle->channels.size());
                    
                    // Init Downsampler
                    int winSize = chConfig.avgSettings.active ? chConfig.avgSettings.windowSize : 1;
                    state.downsampler = std::make_unique<DSP::Downsampler>(winSize);
                    
                    // Init Time Domain State
                    state.lastValue = 0.0;
                    state.hasValue = false;

                    // Init FFT State
                    state.fftActive = chConfig.fftSettings.active;
                    state.hasNewFft = false;
                    state.isMaster = false;

                    if (state.fftActive) {
                        state.fftBuffer = std::make_unique<DSP::FftBuffer>(
                            chConfig.fftSettings.points, 
                            chConfig.fftSettings.overlapPercent
                        );
                        state.fftTransformer = std::make_unique<DSP::FftTransformer>(
                            chConfig.fftSettings.points, 
                            chConfig.fftSettings.windowType
                        );
                    }

                    bundle->channels.push_back(std::move(state));
                    taskMap.slots.push_back(bundle);
                    taskMap.channelToChIdx.push_back(bundle->channels.size() - 1);
                }
            }
            m_taskMappings[task.taskName] = taskMap;
        }

        // 設定 Master Channel
        for (auto &bundle : m_allSlots) {
            if (!bundle->channels.empty()) {
                bundle->masterChannelIdx = 0;
                bundle->channels[0].isMaster = true;
            }
        }
    }

    std::vector<std::string> DspHandler::process(const Data::RawDataChunk &chunk)
    {
        std::vector<std::string> outputPackets;

        if (m_taskMappings.find(chunk.deviceName) == m_taskMappings.end()) return outputPackets;
        TaskMap &map = m_taskMappings[chunk.deviceName];

        size_t totalSamples = chunk.data.size();
        int numChannels = chunk.channelCount;
        if (numChannels == 0) return outputPackets;

        size_t numScans = totalSamples / numChannels;

        // --- 處理迴圈 (Scan by Scan) ---
        for (size_t scan = 0; scan < numScans; ++scan)
        {
            // 我們假設在一個 Scan 內，同一 Slot 的不同通道都屬於同一個取樣時刻
            // 因此可以在遍歷完所有通道後，檢查 Master 是否有產出，來決定要不要發送 UDP

            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (ch >= map.channelToChIdx.size()) break;

                double rawVal = chunk.data[scan * numChannels + ch];
                auto bundle = map.slots[ch];
                int slotChIdx = map.channelToChIdx[ch];
                ChannelState &state = bundle->channels[slotChIdx];

                // 1. 執行降頻 (Moving Average)
                auto maVal = state.downsampler->push(rawVal);

                if (maVal.has_value()) {
                    double val = maVal.value();
                    
                    // [關鍵] 更新時域數值 (Sample & Hold)
                    state.lastValue = val;
                    state.hasValue = true;

                    // 2. 若 FFT 開啟，推入 FFT Buffer
                    if (state.fftActive) {
                        bool ready = state.fftBuffer->push(val);
                        if (ready) {
                            // 立即計算 FFT
                            auto timeData = state.fftBuffer->getData();
                            state.lastFftResult = state.fftTransformer->computeMagnitude(timeData);
                            state.hasNewFft = true;
                        }
                    }

                    // 3. 觸發封包發送邏輯
                    // 只有當 Master Channel 產生新的降頻數值時，才觸發 Slot 的打包
                    if (state.isMaster) {
                        
                        // (A) 產生時域封包 (Time Domain Packet) - 總是產生
                        // 格式: Header, Timestamp, Rate, ChCount, Points(1), [Val0, Val1...]
                        {
                            std::stringstream ss;
                            ss << bundle->slotName 
                               << ",0" // Timestamp placeholder
                               << "," << bundle->effectiveRate
                               << "," << bundle->channels.size()
                               << ",1"; // Points count per packet

                            for (const auto &c : bundle->channels) {
                                // 使用 lastValue (Sample & Hold)
                                if (c.hasValue) ss << "," << c.lastValue;
                                else ss << ",0.0";
                            }
                            outputPackets.push_back(ss.str());
                        }

                        // (B) 產生頻域封包 (FFT Packet) - 僅當 Master FFT 完成時產生
                        // 格式: Header_FFT, Timestamp, Rate, ChCount, BinCount, StartFreq, DeltaFreq, [Data...]
                        if (state.fftActive && state.hasNewFft) {
                            
                            int binCount = state.lastFftResult.size();
                            double deltaFreq = bundle->effectiveRate / state.fftBuffer->getPoints();

                            std::stringstream ss;
                            ss << bundle->slotName << "_FFT" 
                               << ",0"
                               << "," << bundle->effectiveRate
                               << "," << bundle->channels.size()
                               << "," << binCount
                               << ",0.0" // Start Freq
                               << "," << deltaFreq;

                            // 收集所有通道的 FFT 結果
                            for (auto &c : bundle->channels) {
                                // 由於所有通道同步，Master Ready 時其他通道理應也 Ready (或接近 Ready)
                                // 若某通道尚未計算完，使用上一幀或 0
                                if (!c.lastFftResult.empty()) {
                                    for(double binVal : c.lastFftResult) {
                                        ss << "," << std::fixed << std::setprecision(4) << binVal;
                                    }
                                } else {
                                    for(int k=0; k<binCount; k++) ss << ",0";
                                }
                                c.hasNewFft = false; // Reset flag after sending
                            }
                            outputPackets.push_back(ss.str());
                        }
                    }
                }
            }
        }
        return outputPackets;
    }
}
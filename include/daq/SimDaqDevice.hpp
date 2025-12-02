#pragma once
#include "daq/IDaqDevice.hpp"
#include "data/SafeQueue.hpp"
#include "data/DataTypes.hpp"
#include "utils/ConfigLoader.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <cmath>
#include <iostream>
#include <chrono>

namespace DAQ
{

    class SimDaqDevice : public IDaqDevice
    {
    public:
        // [修改] 建構子現在接收 DaqSimulationSettings
        SimDaqDevice(const Utils::TaskConfig &taskConfig,
                     const Utils::DaqSimulationSettings &simSettings,
                     std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue)
            : m_taskConfig(taskConfig),
              m_simSettings(simSettings), // 儲存模擬參數
              m_dataQueue(queue),
              m_isRunning(false),
              m_simTime(0.0)
        {
        }

        ~SimDaqDevice() override { stop(); }

        bool initialize() override
        {
            std::cout << "[SimDAQ] Init Task: " << m_taskConfig.taskName
                      << " | Rate: " << m_taskConfig.sampleRate << " Hz"
                      << " | BaseFreq: " << m_simSettings.baseFrequency << " Hz"
                      << " | Step: " << m_simSettings.frequencyStepPercent << "%"
                      << " | Noise: " << m_simSettings.noisePercent << "%" << std::endl;

            // 計算總通道數
            m_totalChannels = 0;
            for (const auto &ch : m_taskConfig.channels)
            {
                if (ch.active)
                {
                    // 解析 ai0:2 格式
                    int count = 1;
                    if (ch.channelRange.find(':') != std::string::npos)
                    {
                        try
                        {
                            std::string range = ch.channelRange.substr(ch.channelRange.find("ai") + 2);
                            int start = std::stoi(range.substr(0, range.find(':')));
                            int end = std::stoi(range.substr(range.find(':') + 1));
                            count = end - start + 1;
                        }
                        catch (...)
                        {
                            count = 1;
                        }
                    }
                    m_totalChannels += count;
                }
            }
            return true;
        }

        bool start() override
        {
            m_isRunning = true;
            m_workerThread = std::thread(&SimDaqDevice::generateLoop, this);
            return true;
        }

        bool stop() override
        {
            if (m_isRunning)
            {
                m_isRunning = false;
                if (m_workerThread.joinable())
                    m_workerThread.join();
            }
            return true;
        }

    private:
        void generateLoop()
        {
            // 每次模擬產生 0.1 秒的數據塊
            int samplesPerBlock = (int)(m_taskConfig.sampleRate / 10);
            if (samplesPerBlock < 1)
                samplesPerBlock = 1;

            double dt = 1.0 / m_taskConfig.sampleRate;
            const double PI_2 = 2.0 * 3.14159265358979323846;

            while (m_isRunning)
            {
                auto startTick = std::chrono::steady_clock::now();

                std::vector<double> buffer;
                buffer.reserve(samplesPerBlock * m_totalChannels);

                // [修改] 使用 Config 設定的參數生成訊號
                double amp = m_simSettings.amplitude;
                double baseFreq = m_simSettings.baseFrequency;
                double freqStepPct = m_simSettings.frequencyStepPercent;
                double noiseRatio = m_simSettings.noisePercent / 100.0;

                for (int i = 0; i < samplesPerBlock; ++i)
                {
                    double t = m_simTime + i * dt;

                    for (int ch = 0; ch < m_totalChannels; ++ch)
                    {
                        // 1. 計算該通道的頻率 (累加)
                        double channelFreq = baseFreq * (1.0 + (ch * (freqStepPct / 100.0)));

                        // 2. 產生標準弦波
                        double signal = amp * std::sin(PI_2 * channelFreq * t);

                        // 3. 產生雜訊 (均勻分佈 -1 ~ 1 之間，再乘上強度)
                        double randomVal = ((double)std::rand() / RAND_MAX) * 2.0 - 1.0;
                        double noise = amp * noiseRatio * randomVal;

                        buffer.push_back(signal + noise);
                    }
                }

                m_simTime += samplesPerBlock * dt;

                // 打包
                Data::RawDataChunk chunk;
                chunk.deviceName = m_taskConfig.taskName;
                chunk.sampleRate = m_taskConfig.sampleRate;
                chunk.channelCount = m_totalChannels;
                chunk.data = std::move(buffer);

                if (m_dataQueue)
                {
                    m_dataQueue->push(std::move(chunk));
                }

                // 模擬硬體等待 (Sleep)
                auto endTick = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTick - startTick).count();
                int targetSleep = 100; // 100ms

                if (targetSleep > elapsed)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(targetSleep - elapsed));
                }
            }
        }

        Utils::TaskConfig m_taskConfig;
        Utils::DaqSimulationSettings m_simSettings; // [修改] 儲存 DaqSimulationSettings
        std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> m_dataQueue;
        std::atomic<bool> m_isRunning;
        std::thread m_workerThread;
        int m_totalChannels = 0;
        double m_simTime;
    };
}
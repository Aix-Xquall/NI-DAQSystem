#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include "data/SafeQueue.hpp"
#include "data/DataTypes.hpp"
#include "comm/UdpSender.hpp"
#include "utils/ConfigLoader.hpp" // 新增
#include "core/DspHandler.hpp"    // 新增

namespace Core
{
    class DataProcessor
    {
    public:
        // [修改] 建構子需要 SystemConfig 來初始化 DSP
        DataProcessor(std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue,
                      const Utils::SystemConfig &config);
        ~DataProcessor();

        bool start();
        void stop();

    private:
        void processLoop();

        std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> m_queue;
        std::unique_ptr<Comm::UdpSender> m_udpSender;

        // [新增] DSP 處理核心
        std::unique_ptr<DspHandler> m_dspHandler;

        std::thread m_workerThread;
        std::atomic<bool> m_isRunning;
    };
}
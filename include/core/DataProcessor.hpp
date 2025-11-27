#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <string>

#include "data/SafeQueue.hpp"
#include "data/DataTypes.hpp"
#include "comm/UdpSender.hpp"

namespace Core
{

    class DataProcessor
    {
    public:
        // 建構子：注入 Queue 與 UDP 設定
        DataProcessor(std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue,
                      const std::string &udpIp, int udpPort);
        ~DataProcessor();

        // 啟動處理執行緒
        bool start();

        // 停止處理執行緒
        void stop();

    private:
        // 核心工作迴圈
        void processLoop();

        std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> m_queue;
        std::unique_ptr<Comm::UdpSender> m_udpSender; // 擁有 Sender

        std::thread m_workerThread;
        std::atomic<bool> m_isRunning;
    };

}
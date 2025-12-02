#include "core/DataProcessor.hpp"
#include <iostream>
#include <chrono>

namespace Core
{
    DataProcessor::DataProcessor(std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue,
                                 const Utils::SystemConfig &config) // [修改] 傳入完整 Config
        : m_queue(queue), m_isRunning(false)
    {
        // 初始化 UDP Sender
        m_udpSender = std::make_unique<Comm::UdpSender>(config.udpIp, config.udpPort);

        // [新增] 初始化 DSP Handler
        m_dspHandler = std::make_unique<DspHandler>(config);
    }

    DataProcessor::~DataProcessor()
    {
        stop();
    }

    bool DataProcessor::start()
    {
        if (m_isRunning)
            return true;

        if (!m_udpSender->initialize())
        {
            std::cerr << "[Core] Failed to initialize UDP Sender." << std::endl;
            return false;
        }

        m_isRunning = true;
        m_workerThread = std::thread(&DataProcessor::processLoop, this);
        std::cout << "[Core] DataProcessor Started." << std::endl;
        return true;
    }

    void DataProcessor::stop()
    {
        if (!m_isRunning)
            return;
        m_isRunning = false;
        if (m_workerThread.joinable())
            m_workerThread.join();
        std::cout << "[Core] DataProcessor Stopped." << std::endl;
    }

    void DataProcessor::processLoop()
    {
        Data::RawDataChunk chunk;

        while (m_isRunning)
        {
            if (m_queue->try_pop(chunk))
            {
                // [修改] 使用 DSP Handler 處理
                // 這會回傳多個封包 (因為一個 Task 可能拆成多個 Slot，且包含多個時間點)
                std::vector<std::string> packets = m_dspHandler->process(chunk);

                // 逐一發送
                for (const auto &packet : packets)
                {
                    m_udpSender->send(packet);
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
}
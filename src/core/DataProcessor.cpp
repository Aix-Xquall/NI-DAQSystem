#include "core/DataProcessor.hpp"
#include "utils/CsvFormatter.hpp"
#include <iostream>
#include <chrono>

namespace Core
{

    DataProcessor::DataProcessor(std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue,
                                 const std::string &udpIp, int udpPort)
        : m_queue(queue), m_isRunning(false)
    {

        // 初始化 UDP Sender
        m_udpSender = std::make_unique<Comm::UdpSender>(udpIp, udpPort);
    }

    DataProcessor::~DataProcessor()
    {
        stop();
    }

    bool DataProcessor::start()
    {
        if (m_isRunning)
            return true;

        // 初始化 UDP Socket
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
        {
            m_workerThread.join();
        }
        std::cout << "[Core] DataProcessor Stopped." << std::endl;
    }

    void DataProcessor::processLoop()
    {
        Data::RawDataChunk chunk;

        while (m_isRunning)
        {
            // 從 Queue 取出資料 (Non-blocking try_pop)
            if (m_queue->try_pop(chunk))
            {

                // 1. 格式轉換 (使用工具類別)
                // 限制 20 點用於 UI 顯示
                std::string csvPacket = Utils::CsvFormatter::toCsv(chunk, 20);

                // 2. 透過 UDP 發送
                m_udpSender->send(csvPacket);
            }
            else
            {
                // 避免 CPU 100%
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

}
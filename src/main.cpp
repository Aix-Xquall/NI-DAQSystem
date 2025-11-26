#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include "daq/DaqDevice.hpp"
#include "data/SafeQueue.hpp"
#include "data/DataTypes.hpp"

// 消費者執行緒函式
void dataProcessingTask(std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue, std::atomic<bool> &running)
{
    std::cout << "[Consumer] Processing thread started." << std::endl;

    Data::RawDataChunk chunk;
    while (running)
    {
        // 使用 wait_and_pop 避免 CPU 空轉，這是效率最高的等待方式
        // 為了讓執行緒能優雅結束，我們這裡可以改用 try_pop 配合 sleep，或者讓 queue 支援 timeout
        // 為了簡單演示，我們這裡用簡單的輪詢 + sleep (實際專案建議改進 Queue 支援 timeout wait)

        if (queue->try_pop(chunk))
        {
            // 模擬處理數據
            std::cout << "[Data Received] Source: " << chunk.deviceName
                      << " | Samples: " << chunk.samplesPerChannel
                      << " | Total Points: " << chunk.data.size() << std::endl;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    std::cout << "[Consumer] Processing thread stopped." << std::endl;
}

int main()
{
    std::cout << "=== Distributed DAQ System (Multi-threaded) ===" << std::endl;

    // 1. 建立共用的資料佇列 (Thread-Safe)
    auto sharedQueue = std::make_shared<Data::SafeQueue<Data::RawDataChunk>>();

    // 2. 建立 DAQ 物件 (傳入 Queue)
    std::string device1_name = "cDAQ1Mod1"; // NI-9232 (50kHz)
    std::string device2_name = "cDAQ1Mod2"; // NI-9230 (10Hz)

    // NI-9232: 2 channels, 50kHz (每 0.1秒產生 50點 * 2 = 100數據)
    DAQ::DaqDevice daq9232(device1_name, 50.0, 2, sharedQueue);

    // NI-9230: 3 channels, 10Hz (每 0.1秒產生 1點 * 3 = 30數據)
    DAQ::DaqDevice daq9230(device2_name, 10.0, 3, sharedQueue);

    // 3. 初始化
    daq9232.initialize();
    daq9230.initialize();

    // 4. 啟動消費者執行緒
    std::atomic<bool> isSystemRunning(true);
    std::thread consumerThread(dataProcessingTask, sharedQueue, std::ref(isSystemRunning));

    // 5. 啟動 DAQ (生產者)
    daq9232.start();
    daq9230.start();

    // 讓系統跑 5 秒鐘
    std::cout << "\nSystem running for 5 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 6. 停止系統
    std::cout << "\n--- Stopping System ---" << std::endl;
    daq9232.stop();
    daq9230.stop();

    isSystemRunning = false;
    if (consumerThread.joinable())
    {
        consumerThread.join();
    }

    std::cout << "=== End ===" << std::endl;
    std::cin.get();
    return 0;
}
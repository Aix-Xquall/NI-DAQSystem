#include <iostream>
#include <thread>
#include <vector>
#include <memory>
#include "daq/DaqDevice.hpp"
#include "data/SafeQueue.hpp"
#include "core/DataProcessor.hpp"
#include "utils/ConfigLoader.hpp"

int main()
{
    std::cout << "=== NI cDAQ-9189 System (DSP / Downsampling Enabled) ===" << std::endl;

    try
    {
        // 1. 讀取設定
        std::string configPath = "DAQ_Settings.json";
        auto systemConfig = Utils::ConfigLoader::load(configPath);
        std::cout << "[System] Loaded Config: " << systemConfig.systemName << std::endl;

        // 2. 建立 Queue (所有 Task 共用)
        auto sharedQueue = std::make_shared<Data::SafeQueue<Data::RawDataChunk>>();

        // 3. 建立 Tasks (DAQ 擷取端)
        std::vector<std::unique_ptr<DAQ::DaqDevice>> daqTasks;
        for (const auto &taskConfig : systemConfig.taskConfigs)
        {
            // 建立並註冊 Task
            daqTasks.push_back(std::make_unique<DAQ::DaqDevice>(taskConfig, sharedQueue));
            std::cout << "[System] Task Registered: " << taskConfig.taskName
                      << " Rate: " << taskConfig.sampleRate << " Hz" << std::endl;
        }

        // 4. 建立後端處理 (DSP & UDP)
        // [修改] 傳入完整的 systemConfig，以便 DataProcessor 內部建立 DspHandler
        Core::DataProcessor processor(sharedQueue, systemConfig);

        // 5. 初始化與啟動
        std::cout << "--- Initializing Hardware ---" << std::endl;
        for (auto &task : daqTasks)
        {
            if (!task->initialize())
            {
                std::cerr << "[Error] Task init failed. Exiting." << std::endl;
                return -1;
            }
        }

        std::cout << "--- Starting Acquisition & Streaming ---" << std::endl;
        processor.start(); // 啟動 DSP 與 UDP thread
        for (auto &task : daqTasks)
            task->start(); // 啟動 DAQ thread

        std::cout << "System Running. Press Enter to Stop." << std::endl;
        std::cin.get(); // 等待 User 按 Enter

        // 6. 停止
        std::cout << "--- Stopping ---" << std::endl;
        for (auto &task : daqTasks)
            task->stop();
        processor.stop();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
    }
    return 0;
}
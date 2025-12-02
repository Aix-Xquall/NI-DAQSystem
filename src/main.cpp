#include <iostream>
#include <thread>
#include <vector>
#include <memory>
#include "daq/IDaqDevice.hpp"
#include "daq/DaqDevice.hpp"
#include "daq/SimDaqDevice.hpp"
#include "data/SafeQueue.hpp"
#include "core/DataProcessor.hpp"
#include "utils/ConfigLoader.hpp"

int main()
{
    std::cout << "=== NI cDAQ System (Sim/Real Configurable) ===" << std::endl;

    try
    {
        // 1. 讀取設定
        std::string configPath = "DAQ_Settings.json";
        auto systemConfig = Utils::ConfigLoader::load(configPath);

        std::cout << "[System] Loaded Config: " << systemConfig.systemName << std::endl;

        // [修改] 檢查模擬開關 (改為 daqSimConfig)
        bool isSimMode = systemConfig.daqSimConfig.active;
        if (isSimMode)
        {
            std::cout << ">>> SIMULATION MODE ACTIVE <<<" << std::endl;
            std::cout << "   Base Freq: " << systemConfig.daqSimConfig.baseFrequency << "Hz" << std::endl;
            std::cout << "   Noise: " << systemConfig.daqSimConfig.noisePercent << "%" << std::endl;
        }

        // 2. 建立 Queue
        auto sharedQueue = std::make_shared<Data::SafeQueue<Data::RawDataChunk>>();

        // 3. 建立 Tasks
        // [修改] 使用 IDaqDevice 介面以支援多型
        std::vector<std::unique_ptr<DAQ::IDaqDevice>> daqTasks;

        for (const auto &taskConfig : systemConfig.taskConfigs)
        {
            if (isSimMode)
            {
                // [修改] 傳入 daqSimConfig 參數
                daqTasks.push_back(std::make_unique<DAQ::SimDaqDevice>(
                    taskConfig,
                    systemConfig.daqSimConfig,
                    sharedQueue));
            }
            else
            {
                // 真實 DAQ
                daqTasks.push_back(std::make_unique<DAQ::DaqDevice>(
                    taskConfig,
                    sharedQueue));
            }
            std::cout << "[Factory] Created Task: " << taskConfig.taskName << std::endl;
        }

        // 4. 建立後端
        Core::DataProcessor processor(sharedQueue, systemConfig);

        // 5. 初始化與啟動
        std::cout << "--- Initializing ---" << std::endl;
        for (auto &task : daqTasks)
        {
            if (!task->initialize())
            {
                std::cerr << "[Error] Task init failed." << std::endl;
                return -1;
            }
        }

        std::cout << "--- Starting ---" << std::endl;
        processor.start();
        for (auto &task : daqTasks)
            task->start();

        std::cout << "System Running. Press Enter to Stop." << std::endl;
        std::cin.get();

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
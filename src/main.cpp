#include <iostream>
#include <thread>
#include <vector>
#include <memory>
#include "daq/DaqDevice.hpp"
#include "data/SafeQueue.hpp"
#include "core/DataProcessor.hpp" // 假設您已有此檔案
#include "utils/ConfigLoader.hpp"

int main() {
    std::cout << "=== NI cDAQ-9189 System (With Active/Inactive Control) ===" << std::endl;

    try {
        // 1. 讀取設定
        std::string configPath = "DAQ_Settings.json";
        auto systemConfig = Utils::ConfigLoader::load(configPath);
        std::cout << "[System] Loaded Config: " << systemConfig.systemName << std::endl;

        // 2. 建立 Queue
        auto sharedQueue = std::make_shared<Data::SafeQueue<Data::RawDataChunk>>();

        // 3. 建立 Tasks
        std::vector<std::unique_ptr<DAQ::DaqDevice>> daqTasks;
        for (const auto& taskConfig : systemConfig.taskConfigs) {
            daqTasks.push_back(std::make_unique<DAQ::DaqDevice>(taskConfig, sharedQueue));
            std::cout << "[System] Task Registered: " << taskConfig.taskName << std::endl;
        }

        // 4. 建立後端
        Core::DataProcessor processor(sharedQueue, systemConfig.udpIp, systemConfig.udpPort);

        // 5. 初始化與啟動
        std::cout << "--- Initializing ---" << std::endl;
        for (auto& task : daqTasks) {
            if (!task->initialize()) {
                std::cerr << "[Error] Task init failed. Exiting." << std::endl;
                return -1;
            }
        }

        std::cout << "--- Starting ---" << std::endl;
        processor.start();
        for (auto& task : daqTasks) task->start();

        std::cout << "System Running. Press Enter to Stop." << std::endl;
        std::cin.get();

        // 6. 停止
        std::cout << "--- Stopping ---" << std::endl;
        for (auto& task : daqTasks) task->stop();
        processor.stop();

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
    }
    return 0;
}
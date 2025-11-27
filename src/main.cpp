#include <iostream>
#include <thread>
#include <memory>
#include <vector>
#include <string>

// 引入各模組
#include "daq/DaqDevice.hpp"
#include "data/SafeQueue.hpp"
#include "core/DataProcessor.hpp"
#include "utils/ConfigLoader.hpp"

int main()
{
    std::cout << "=== Distributed DAQ System (Configurable) ===" << std::endl;

    try
    {
        // --- 1. 讀取設定檔 ---
        // 假設設定檔在執行檔同層，或上一層
        // 在 VS Code 開發環境中，通常是 "${workspaceFolder}/DAQ_Settings.json"
        // 編譯後執行檔在 build/，所以設定檔可能在 ../DAQ_Settings.json
        std::string configPath = "../DAQ_Settings.json";

        std::cout << "[System] Loading config from: " << configPath << std::endl;
        auto systemConfig = Utils::ConfigLoader::load(configPath);

        std::cout << "[System] Config Loaded. System Name: " << systemConfig.systemName << std::endl;

        // --- 2. 建立資料管線 ---
        auto sharedQueue = std::make_shared<Data::SafeQueue<Data::RawDataChunk>>();

        // --- 3. 動態建立 DAQ 物件 (使用 vector 管理) ---
        // 使用 unique_ptr 來管理物件生命週期
        std::vector<std::unique_ptr<DAQ::DaqDevice>> daqDevices;

        for (const auto &daqConfig : systemConfig.daqConfigs)
        {
            // 建立物件並存入 vector
            // std::make_unique<Type>(constructor_args...)
            daqDevices.push_back(std::make_unique<DAQ::DaqDevice>(daqConfig, sharedQueue));
            std::cout << "[System] Device registered: " << daqConfig.deviceName
                      << " [" << daqConfig.channelRange << "]" << std::endl;
        }

        // --- 4. 建立核心處理器 ---
        Core::DataProcessor processor(sharedQueue, systemConfig.udpIp, systemConfig.udpPort);

        // --- 5. 初始化所有裝置 ---
        std::cout << "\n--- System Initialization ---" << std::endl;
        for (auto &device : daqDevices)
        {
            device->initialize();
        }

        // --- 6. 啟動系統 ---
        std::cout << "\n--- System Start ---" << std::endl;
        processor.start(); // 先啟動後端

        for (auto &device : daqDevices)
        {
            device->start();
        }

        // --- 7. 運行等待 ---
        std::cout << "\nSystem is running... (Press 'Enter' to stop)" << std::endl;
        std::cin.get();

        // --- 8. 停止系統 ---
        std::cout << "\n--- System Stop ---" << std::endl;

        for (auto &device : daqDevices)
        {
            device->stop();
        }
        processor.stop();

        // vector 清空時，unique_ptr 會自動釋放記憶體
        daqDevices.clear();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[FATAL ERROR] " << e.what() << std::endl;
        std::cout << "Press Enter to exit...";
        std::cin.get();
        return -1;
    }

    std::cout << "=== Bye ===" << std::endl;
    return 0;
}
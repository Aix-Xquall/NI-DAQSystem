#pragma once

#include <string>
#include <vector>
#include <thread>                 // 新增
#include <atomic>                 // 新增
#include <memory>                 // 新增
#include "NIDAQmx.h"              // 跨平台時，Linux 路徑可能不同，CMake 會處理 Include Path
#include "data/DataTypes.hpp"     // 新增
#include "data/SafeQueue.hpp"     // 新增
#include "utils/ConfigLoader.hpp" // 引入 Config 定義

namespace DAQ
{

    class DaqDevice
    {
    public:
        // 建構子：傳入共用的 Queue 指標、裝置名稱 (如 "cDAQ1Mod1") 取樣率

        DaqDevice(const Utils::DaqConfig &config,
                  std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue);
        ~DaqDevice();

        // 初始化 Task (設定通道、時脈)
        bool initialize();

        // 開始擷取
        bool start();

        // 停止擷取
        bool stop();

        // 取得裝置名稱 (Debug用)
        std::string getName() const;

    private:
        // NI-DAQmx Task Handle (這是核心控制代碼)
        TaskHandle m_taskHandle;
        // 儲存設定參數
        Utils::DaqConfig m_config;

        bool m_isInitialized;

        // 執行緒控制
        std::atomic<bool> m_isRunning; // 使用 atomic 確保執行緒安全標記
        std::thread m_workerThread;    // 背景讀取執行緒

        // 資料佇列 (所有 DAQ 共用這個 Queue)
        std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> m_dataQueue;

        // 內部錯誤處理 helper
        void checkError(int32 error);

        // 這是真正的背景工作函式
        void readLoop();
    };

} // namespace DAQ
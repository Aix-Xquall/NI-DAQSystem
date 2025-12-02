#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include "daq/IDaqDevice.hpp"
#include "NIDAQmx.h"
#include "data/DataTypes.hpp"
#include "data/SafeQueue.hpp"
#include "utils/ConfigLoader.hpp"

namespace DAQ
{
    // 繼承 IDaqDevice
    class DaqDevice : public IDaqDevice
    {
    public:
        // 建構子：傳入共用的 Queue 指標、裝置名稱 (如 "cDAQ1Mod1") 取樣率

        // 改為傳入 TaskConfig
        DaqDevice(const Utils::TaskConfig &config,
                  std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue);
        ~DaqDevice() override;

        // 初始化 Task (設定通道、時脈)
        bool initialize() override;

        // 開始擷取
        bool start() override;

        // 停止擷取
        bool stop() override;

    private:
        // NI-DAQmx Task Handle (這是核心控制代碼)
        TaskHandle m_taskHandle;
        // 儲存設定參數
        Utils::TaskConfig m_config; // 儲存 Task 設定

        bool m_isInitialized;

        // 執行緒控制
        std::atomic<bool> m_isRunning; // 使用 atomic 確保執行緒安全標記
        std::thread m_workerThread;    // 背景讀取執行緒

        // 資料佇列 (所有 DAQ 共用這個 Queue)
        std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> m_dataQueue;

        // 實際建立的通道總數 (用於 Buffer 計算)
        int m_totalChannelCount;

        // 內部錯誤處理 helper
        void checkError(int32 error);

        // 這是真正的背景工作函式
        void readLoop();
    };

} // namespace DAQ
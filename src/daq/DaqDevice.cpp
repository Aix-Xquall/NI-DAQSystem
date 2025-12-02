#include "daq/DaqDevice.hpp"
#include <iostream>
#include <vector>

namespace DAQ
{

    // 建構子：初始化成員變數
    DaqDevice::DaqDevice(const Utils::TaskConfig &config,
                         std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue)
        : m_config(config),
          m_taskHandle(0), m_isInitialized(false), m_isRunning(false),
          m_dataQueue(queue), m_totalChannelCount(0)
    {
    }

    // 解構子：確保資源釋放
    DaqDevice::~DaqDevice()
    {
        stop();
        if (m_taskHandle != 0)
        {
            DAQmxClearTask(m_taskHandle);
        }
    }

    // 初始化 DAQ 硬體
    bool DaqDevice::initialize()
    {
        if (m_isInitialized)
            return true;

        std::cout << "[DAQ] Init Task: " << m_config.taskName << " (" << m_config.sampleRate << " Hz)..." << std::endl;

        // 1. 建立 NI-DAQmx Task
        int32 error = DAQmxCreateTask(m_config.taskName.c_str(), &m_taskHandle);
        if (error != 0)
        {
            checkError(error);
            return false;
        }

        m_totalChannelCount = 0;

        // 2. 遍歷設定檔中的所有通道 (Channels)
        for (const auto &chConfig : m_config.channels)
        {

            // [Check] 檢查 active 欄位，若為 false 則跳過此通道不建立
            if (!chConfig.active)
            {
                std::cout << "  -> [Skip] Inactive Device: " << chConfig.deviceName << std::endl;
                continue;
            }

            // 組合完整路徑字串 (例如 "cDAQ1Mod1/ai0:2")
            std::string fullPath = chConfig.deviceName + "/" + chConfig.channelRange;
            std::cout << "  -> Config: " << fullPath << " [" << chConfig.channelType << "]";

            // =========================================================
            // Case 1: 電壓/IEPE 通道 (適用 NI-9230 / 9231 / 9232 / 9239)
            // =========================================================
            if (chConfig.channelType == "Voltage_IEPE" || chConfig.channelType == "Voltage")
            {

                // 1-1. 建立基本的電壓輸入通道
                // minVal/maxVal 必須符合該模組的物理範圍 (9231為+/-5V, 9230/32為+/-30V)
                error = DAQmxCreateAIVoltageChan(
                    m_taskHandle, fullPath.c_str(), "",
                    DAQmx_Val_Cfg_Default,
                    chConfig.minVal, chConfig.maxVal,
                    DAQmx_Val_Volts, NULL);

                // 1-2. 設定 IEPE (若設定檔中有啟用電流)
                if (error == 0 && chConfig.channelType == "Voltage_IEPE" && chConfig.iepeSettings.current > 0.0)
                {

                    // [修正點] 設定 IEPE 激發源為「內部 (Internal)」
                    // 這一步告訴 DAQ 卡開啟 IEPE 供電電路
                    error = DAQmxSetAIExcitSrc(m_taskHandle, fullPath.c_str(), DAQmx_Val_Internal);

                    if (error == 0)
                    {
                        // [修正點] 設定 IEPE 電流值 (單位: 安培)
                        // NI-9232/9230 支援 0.004 (4mA)
                        // NI-9231 支援 0.002 (2mA)
                        error = DAQmxSetAIExcitVal(m_taskHandle, fullPath.c_str(), chConfig.iepeSettings.current);
                    }
                }

                // 1-3. 設定耦合 (AC / DC)
                if (error == 0 && chConfig.channelType == "Voltage_IEPE")
                {
                    if (chConfig.iepeSettings.coupling == "AC")
                    {
                        error = DAQmxSetAICoupling(m_taskHandle, fullPath.c_str(), DAQmx_Val_AC);
                    }
                    else
                    {
                        error = DAQmxSetAICoupling(m_taskHandle, fullPath.c_str(), DAQmx_Val_DC);
                    }
                }
            }
            // =========================================================
            // Case 2: 應變規通道 (適用 NI-9235 Quarter Bridge)
            // =========================================================
            else if (chConfig.channelType == "Strain_Bridge")
            {
                // NI-9235 固定為 QuarterBridgeI (或依手冊 II)，內建 120 Ohm
                // 注意：minVal/maxVal 單位是 Strain (例如 0.02)，不是 Volts
                error = DAQmxCreateAIStrainGageChan(
                    m_taskHandle, fullPath.c_str(), "",
                    chConfig.minVal, chConfig.maxVal,
                    DAQmx_Val_Strain,                      // 單位設定
                    DAQmx_Val_QuarterBridgeI,              // 橋接配置
                    DAQmx_Val_Internal,                    // 內部激發
                    chConfig.strainSettings.excitationVal, // 激發電壓 (9235 通常 2.5V)
                    chConfig.strainSettings.gageFactor,    // GF (通常 2.0)
                    0.0,                                   // Initial Bridge Voltage
                    chConfig.strainSettings.resistance,    // 標稱電阻 (120.0)
                    chConfig.strainSettings.poissonRatio,  // 泊松比
                    0.0,                                   // 導線電阻
                    NULL);
            }
            // =========================================================
            // Case 3: 熱電偶通道 (適用 NI-9214)
            // =========================================================
            else if (chConfig.channelType == "Thermocouple_K")
            {
                // 單位必須是 DegC (攝氏度)
                // CJC 來源設為 BuiltIn (內建冷接點補償)
                error = DAQmxCreateAIThrmcplChan(
                    m_taskHandle, fullPath.c_str(), "",
                    chConfig.minVal, chConfig.maxVal,
                    DAQmx_Val_DegC,
                    DAQmx_Val_K_Type_TC,
                    DAQmx_Val_BuiltIn,
                    0.0, "");
            }
            // 未知類型處理
            else
            {
                std::cerr << " -> [Error] Unknown Channel Type: " << chConfig.channelType << std::endl;
                return false;
            }

            // 錯誤檢查
            if (error != 0)
            {
                checkError(error);
                std::cout << " -> FAILED" << std::endl;
                return false;
            }
            std::cout << " -> OK" << std::endl;
        }

        // 3. 確認最終建立的通道數 (重要：用於後續 Buffer 計算)
        uInt32 actualChans = 0;
        DAQmxGetTaskNumChans(m_taskHandle, &actualChans);
        m_totalChannelCount = (int)actualChans;

        if (m_totalChannelCount == 0)
        {
            std::cerr << "[Warning] Task created but no channels are active." << std::endl;
            // 這裡可以選擇 return false，或允許空 Task (視需求而定)
            return false;
        }

        // 4. 設定取樣時脈 (Sample Clock)
        // Buffer Size 策略：取樣率的 20 倍，確保有 20 秒的緩衝空間，避免 Overwrite 錯誤
        uInt64 bufferSize = (uInt64)m_config.sampleRate * 20;

        error = DAQmxCfgSampClkTiming(
            m_taskHandle,
            "", // 外部時鐘源 (空字串代表使用內部 OnboardClock)
            m_config.sampleRate,
            DAQmx_Val_Rising,
            DAQmx_Val_ContSamps, // 連續取樣模式
            bufferSize);
        if (error != 0)
        {
            checkError(error);
            return false;
        }

        m_isInitialized = true;
        return true;
    }

    // 啟動 Task
    bool DaqDevice::start()
    {
        if (!m_isInitialized)
            return false;
        if (m_isRunning)
            return true;

        int32 error = DAQmxStartTask(m_taskHandle);
        if (error != 0)
        {
            checkError(error);
            return false;
        }

        m_isRunning = true;
        m_workerThread = std::thread(&DaqDevice::readLoop, this);
        return true;
    }

    // 停止 Task
    bool DaqDevice::stop()
    {
        if (!m_isRunning)
            return true;

        m_isRunning = false; // 通知執行緒結束
        if (m_workerThread.joinable())
        {
            m_workerThread.join();
        }

        if (m_taskHandle != 0)
        {
            DAQmxStopTask(m_taskHandle);
            // 注意：ClearTask 在解構子或下一次 Initialize 前執行
        }
        return true;
    }

    // 背景讀取迴圈
    void DaqDevice::readLoop()
    {
        // 設定每次讀取的區塊大小 (Block Size)
        // 為了降低 CPU loading，建議每 0.1 秒讀取一次 (即取樣率 / 10)
        int samplesPerRead = (int)(m_config.sampleRate / 10);
        if (samplesPerRead < 1)
            samplesPerRead = 1;

        // 準備接收 Buffer (大小 = 點數 * 通道數)
        std::vector<double> buffer(samplesPerRead * m_totalChannelCount);
        int32 readSamples = 0;
        int32 error = 0;

        while (m_isRunning)
        {
            // 讀取 DAQ 數據 (Blocking Call, Timeout 10s)
            error = DAQmxReadAnalogF64(
                m_taskHandle,
                samplesPerRead,
                10.0,
                DAQmx_Val_GroupByScanNumber, // 資料排列: Ch0, Ch1, Ch0, Ch1...
                buffer.data(),
                buffer.size(),
                &readSamples,
                NULL);

            if (error != 0)
            {
                checkError(error);
                // 發生嚴重錯誤時跳出迴圈
                if (error < 0)
                    break;
            }
            else if (readSamples > 0)
            {
                // 封裝數據
                Data::RawDataChunk chunk;
                chunk.deviceName = m_config.taskName;
                chunk.sampleRate = m_config.sampleRate;
                chunk.channelCount = m_totalChannelCount;
                chunk.data = buffer; // Vector copy occurs here

                // 推送至 Queue
                if (m_dataQueue)
                {
                    m_dataQueue->push(std::move(chunk));
                }
            }
        }
    }

    // 錯誤處理 Helper
    void DaqDevice::checkError(int32 error)
    {
        if (error != 0)
        {
            char errBuff[2048] = {'\0'};
            DAQmxGetExtendedErrorInfo(errBuff, 2048);
            std::cerr << "[DAQ Error] Task: " << m_config.taskName
                      << " Code: " << error << " Msg: " << errBuff << std::endl;
        }
    }
}
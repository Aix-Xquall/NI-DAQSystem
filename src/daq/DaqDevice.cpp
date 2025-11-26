#include "daq/DaqDevice.hpp"
#include <iostream>
#include <vector>

namespace DAQ
{

    DaqDevice::DaqDevice(const std::string &deviceName, double sampleRate, int channelCount,
                         std::shared_ptr<Data::SafeQueue<Data::RawDataChunk>> queue)
        : m_deviceName(deviceName),
          m_sampleRate(sampleRate),
          m_channelCount(channelCount),
          m_taskHandle(0),
          m_isInitialized(false),
          m_isRunning(false),
          m_dataQueue(queue)
    {
    }

    DaqDevice::~DaqDevice()
    {
        stop(); // 確保解構時停止任務
        if (m_taskHandle != 0)
        {
            DAQmxClearTask(m_taskHandle);
        }
    }

    bool DaqDevice::initialize()
    {
        if (m_isInitialized)
            return true;

        std::cout << "[DAQ] Initializing " << m_deviceName << " at " << m_sampleRate << " Hz..." << std::endl;

        // 1. 建立 Task
        // 空字串代表讓 NI 自動產生 Task Name，避免衝突
        int32 error = DAQmxCreateTask("", &m_taskHandle);
        if (error != 0)
        {
            checkError(error);
            return false;
        }

        // 2. 建立類比輸入通道 (Analog Input Voltage Channel)
        // 語法範例: "cDAQ1Mod1/ai0:1" 代表該模組的 ai0 到 ai1
        // 為了通用性，我們這邊先簡單組合成 "Device/ai0:N-1"
        // 實際專案中可能需要更靈活的通道設定字串
        std::string channelString = m_deviceName + "/ai0:" + std::to_string(m_channelCount - 1);

        // 設定範圍 -10V 到 10V (NI-9232/9230 通用範圍，可依需求調整)
        error = DAQmxCreateAIVoltageChan(m_taskHandle, channelString.c_str(), "",
                                         DAQmx_Val_Cfg_Default, -10.0, 10.0, DAQmx_Val_Volts, NULL);
        if (error != 0)
        {
            checkError(error);
            return false;
        }

        // 3. 設定取樣時脈 (Timing)
        // DAQmx_Val_ContSamps: 連續取樣模式
        // bufferSize 設定為取樣率的 10 倍 (經驗法則，避免 Overflow)
        uInt64 samplesPerChan = (uInt64)m_sampleRate * 10;

        error = DAQmxCfgSampClkTiming(m_taskHandle, "", m_sampleRate, DAQmx_Val_Rising,
                                      DAQmx_Val_ContSamps, samplesPerChan);
        if (error != 0)
        {
            checkError(error);
            return false;
        }

        m_isInitialized = true;
        std::cout << "[DAQ] " << m_deviceName << " Initialized Successfully." << std::endl;
        return true;
    }

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

        // 啟動背景執行緒
        m_isRunning = true;
        m_workerThread = std::thread(&DaqDevice::readLoop, this);

        std::cout << "[DAQ] " << m_deviceName << " Started." << std::endl;
        return true;
    }

    bool DaqDevice::stop()
    {
        if (!m_isRunning)
            return true;

        m_isRunning = false; // 通知 thread 結束迴圈

        if (m_taskHandle != 0)
        {
            DAQmxStopTask(m_taskHandle);
        }

        std::cout << "[DAQ] " << m_deviceName << " Stopped." << std::endl;
        return true;
    }

    // --- 核心讀取迴圈 ---
    void DaqDevice::readLoop()
    {
        int32 error = 0;
        int32 readSamples = 0;

        // 設定每次讀取的樣本數 (Block Size)
        // 對於 50kHz，我們可能每 0.1秒讀一次 (5000點) 比較有效率
        // 對於 10Hz，可能每 1秒讀一次 (10點) 或每 0.1秒讀 (1點)
        // 這裡簡單設定為取樣率的 1/10 (即 100ms 的數據量)
        int samplesPerRead = (int)(m_sampleRate / 10);
        if (samplesPerRead < 1)
            samplesPerRead = 1;

        // 緩衝區大小 = 樣本數 * 通道數
        std::vector<double> buffer(samplesPerRead * m_channelCount);

        while (m_isRunning)
        {
            // DAQmxReadAnalogF64
            // timeout 設定為 10.0 秒 (避免卡死)
            // DAQmx_Val_GroupByScanNumber: 資料排列方式 (Interleaved: ch0, ch1, ch0, ch1...)
            error = DAQmxReadAnalogF64(m_taskHandle, samplesPerRead, 10.0,
                                       DAQmx_Val_GroupByScanNumber,
                                       buffer.data(), buffer.size(),
                                       &readSamples, NULL);

            if (error != 0)
            {
                checkError(error);
                // 如果發生嚴重錯誤，可能需要跳出迴圈
                if (error < 0)
                    break;
            }
            else if (readSamples > 0)
            {
                // 讀取成功，封裝數據
                Data::RawDataChunk chunk;
                chunk.deviceName = m_deviceName;
                chunk.timestamp = std::chrono::system_clock::now();
                chunk.data = buffer; // 這裡會發生一次 copy，在功能驗證階段可接受
                chunk.channelCount = m_channelCount;
                chunk.samplesPerChannel = readSamples;
                chunk.sampleRate = m_sampleRate;

                // 推送到佇列 (Thread-Safe)
                if (m_dataQueue)
                {
                    m_dataQueue->push(std::move(chunk));
                }
            }
        }
    }

    std::string DaqDevice::getName() const
    {
        return m_deviceName;
    }

    void DaqDevice::checkError(int32 error)
    {
        if (error != 0)
        {
            char errBuff[2048] = {'\0'};
            DAQmxGetExtendedErrorInfo(errBuff, 2048);
            std::cerr << "[DAQ ERROR] Device: " << m_deviceName << " Code: " << error << "\n"
                      << "Message: " << errBuff << std::endl;
        }
    }

} // namespace DAQ
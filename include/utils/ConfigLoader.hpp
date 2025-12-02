#pragma once
#include <string>
#include <vector>

namespace Utils
{

    struct IepeSettings
    {
        double current;
        std::string coupling;
    };

    struct StrainSettings
    {
        double gageFactor;
        double resistance;
        double poissonRatio;
        double excitationVal;
    };

    // [新增] Moving Average 設定
    struct MovingAvgSettings
    {
        bool active;
        int windowSize; // 降頻倍率 (例如: HW 10k / Target 5k = 2)
    };

    // [新增] FFT 設定 (預留擴充)
    struct FftSettings
    {
        bool active;
        std::string windowType; // e.g., "Hann", "Blackman"
        int points;             // e.g., 1024
    };

    struct ChannelConfig
    {
        std::string deviceName;
        std::string modelInfo;
        std::string channelRange;
        std::string channelType;

        bool active;

        double minVal;
        double maxVal;

        // 參數區
        IepeSettings iepeSettings;
        StrainSettings strainSettings;

        // [新增] DSP 參數
        MovingAvgSettings avgSettings;
        FftSettings fftSettings;
    };

    struct TaskConfig
    {
        std::string taskName;
        bool active;
        double sampleRate; // 硬體取樣率 (HW Rate)
        std::vector<ChannelConfig> channels;
    };

    struct SystemConfig
    {
        std::string systemName;
        std::string udpIp;
        int udpPort;
        std::vector<TaskConfig> taskConfigs;
    };

    class ConfigLoader
    {
    public:
        static SystemConfig load(const std::string &filePath);
    };
}
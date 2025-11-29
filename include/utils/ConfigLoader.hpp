#pragma once
#include <string>
#include <vector>

namespace Utils {

    struct IepeSettings {
        double current;       // IEPE 電流 (A)
        std::string coupling; // "AC" or "DC"
    };

    struct StrainSettings {
        double gageFactor;
        double resistance;
        double poissonRatio;
        double excitationVal;
    };

    struct ChannelConfig {
        std::string deviceName;    // 硬體位址: cDAQ1Mod1
        std::string modelInfo;     // 識別資訊: NI-9232
        std::string channelRange;  // ai0:1
        std::string channelType;   // Voltage_IEPE, Strain_Bridge, Thermocouple_K
        
        bool active;               // [新增] 單一通道/模組啟用開關

        double minVal;             // 依據類型不同，單位可能是 V, Strain, 或 DegC
        double maxVal;

        // 專用參數區
        IepeSettings iepeSettings;
        StrainSettings strainSettings;
    };

    struct TaskConfig {
        std::string taskName;
        bool active;               // Task 整體開關
        double sampleRate;
        std::vector<ChannelConfig> channels;
    };

    struct SystemConfig {
        std::string systemName;
        std::string udpIp;
        int udpPort;
        std::vector<TaskConfig> taskConfigs;
    };

    class ConfigLoader {
    public:
        static SystemConfig load(const std::string& filePath);
    };
}
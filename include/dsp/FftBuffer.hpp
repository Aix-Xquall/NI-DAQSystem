#pragma once
#include <vector>
#include <deque>
#include <cmath>

namespace DSP
{

    /**
     * @brief FFT 資料緩衝區 (Sliding Window with Overlap)
     * 負責收集數據，當滿足 (Points) 長度時觸發一次計算，
     * 並根據 Overlap 保留部分數據給下一次使用。
     */
    class FftBuffer
    {
    public:
        FftBuffer(int points, double overlapPercent)
            : m_points(points), m_samplesSinceLastTrigger(0)
        {

            // 計算 Hop Size (每次移動多少點)
            // Hop = N * (1 - overlap%)
            // 例如 1024 點, 25% overlap => hop = 768 點 (保留 256 點)
            double overlapRatio = overlapPercent / 100.0;
            m_hopSize = static_cast<int>(points * (1.0 - overlapRatio));
            if (m_hopSize < 1)
                m_hopSize = 1;

            // 預留空間
            m_buffer.reserve(points);
        }

        /**
         * @brief 推送一個數據點
         * @return true 若緩衝區已滿且滿足 Hop Size (可以進行 FFT 計算)
         */
        bool push(double value)
        {
            // 1. 放入數據
            m_buffer.push_back(value);
            m_samplesSinceLastTrigger++;

            // 2. 檢查是否滿了
            if (static_cast<int>(m_buffer.size()) > m_points)
            {
                // 如果超過大小，移除最舊的 (保持由最新的 points 組成)
                // 這是一種簡單的實作，實際上我們只在觸發後移除
                m_buffer.erase(m_buffer.begin());
            }

            // 3. 檢查觸發條件
            // 條件 A: Buffer 填滿了 N 點
            // 條件 B: 距離上次觸發已經過了 Hop Size
            bool ready = false;
            if (static_cast<int>(m_buffer.size()) == m_points)
            {
                if (m_firstTrigger)
                {
                    ready = true;
                    m_firstTrigger = false;
                    m_samplesSinceLastTrigger = 0;
                }
                else if (m_samplesSinceLastTrigger >= m_hopSize)
                {
                    ready = true;
                    m_samplesSinceLastTrigger = 0;
                }
            }
            return ready;
        }

        // 取得當前 Buffer 內的資料 (用於計算)
        const std::vector<double> &getData() const
        {
            return m_buffer;
        }

        int getPoints() const { return m_points; }

    private:
        int m_points;
        int m_hopSize;
        int m_samplesSinceLastTrigger;
        bool m_firstTrigger = true;
        std::vector<double> m_buffer;
    };
}
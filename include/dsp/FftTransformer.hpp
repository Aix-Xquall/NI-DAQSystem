#pragma once
#include <vector>
#include <string>
#include <complex>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace DSP
{

    class FftTransformer
    {
    public:
        FftTransformer(int points, const std::string &windowType)
            : m_points(points)
        {
            initWindow(windowType);
        }

        /**
         * @brief 執行 FFT 並計算 Magnitude
         * @param input 時域資料 (長度必須等於 points)
         * @return 頻域 Magnitude 資料 (長度為 points/2 + 1)
         */
        std::vector<double> computeMagnitude(const std::vector<double> &input)
        {
            if (input.size() != m_points)
                return {};

            // 1. Apply Window Function & Copy to Complex Buffer
            std::vector<std::complex<double>> complexBuf(m_points);
            for (int i = 0; i < m_points; ++i)
            {
                complexBuf[i] = input[i] * m_window[i];
            }

            // 2. Compute FFT (In-place)
            fft(complexBuf);

            // 3. Compute Magnitude (Only first N/2 + 1 points)
            // Magnitude = sqrt(re^2 + im^2) / N (Normalize)
            // *注意* : 通常工程上會只取單邊頻譜並乘 2 (除 DC 與 Nyquist)
            int halfPoints = m_points / 2 + 1;
            std::vector<double> magnitude(halfPoints);

            double normalizer = 2.0 / m_points;

            for (int i = 0; i < halfPoints; ++i)
            {
                double mag = std::abs(complexBuf[i]) * normalizer;
                if (i == 0 || i == m_points / 2)
                    mag /= 2.0; // DC 和 Nyquist 不乘 2
                magnitude[i] = mag;
            }

            return magnitude;
        }

    private:
        int m_points;
        std::vector<double> m_window;

        void initWindow(const std::string &type)
        {
            m_window.resize(m_points);
            for (int i = 0; i < m_points; ++i)
            {
                if (type == "Hann")
                {
                    m_window[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (m_points - 1)));
                }
                else if (type == "Blackman")
                {
                    m_window[i] = 0.42 - 0.5 * std::cos(2.0 * M_PI * i / (m_points - 1)) + 0.08 * std::cos(4.0 * M_PI * i / (m_points - 1));
                }
                else
                {
                    m_window[i] = 1.0; // Rectangular (None)
                }
            }
        }

        // 簡單的 Radix-2 Cooley-Tukey FFT (Recursive)
        // 注意：為了 Clean Code 與不依賴外部庫，這裡使用簡易實作。
        // 在 Linux RT Production 環境建議替換為 FFTW3。
        void fft(std::vector<std::complex<double>> &x)
        {
            const size_t N = x.size();
            if (N <= 1)
                return;

            // Split into even and odd
            std::vector<std::complex<double>> even(N / 2);
            std::vector<std::complex<double>> odd(N / 2);
            for (size_t i = 0; i < N / 2; ++i)
            {
                even[i] = x[2 * i];
                odd[i] = x[2 * i + 1];
            }

            // Recursion
            fft(even);
            fft(odd);

            // Combine
            for (size_t k = 0; k < N / 2; ++k)
            {
                std::complex<double> t = std::polar(1.0, -2.0 * M_PI * k / N) * odd[k];
                x[k] = even[k] + t;
                x[k + N / 2] = even[k] - t;
            }
        }
    };
}
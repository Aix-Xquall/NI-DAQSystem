cmake_minimum_required(VERSION 3.10)

# 專案名稱與版本
project(NI-DAQSystem VERSION 0.1.0 LANGUAGES CXX)

# 設定 C++ 標準 (使用 C++17 以支援現代語法)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- NI-DAQmx 路徑設定 ---
# 使用你提供的路徑
set(NI_DAQ_ROOT "C:/Program Files (x86)/National Instruments/NI-DAQ/DAQmx ANSI C Dev")

# 設定 Include 路徑 (讓程式找得到 NIDAQmx.h)
include_directories("${NI_DAQ_ROOT}/include")

# 讓編譯器找得到 include/ 資料夾內的標頭檔
include_directories("${PROJECT_SOURCE_DIR}/include")
#include_directories("${PROJECT_SOURCE_DIR}/include/daq")
#include_directories("${PROJECT_SOURCE_DIR}/include/data")

# 設定 Library 路徑
# 注意：MinGW 32-bit 需連結 32-bit 的 NI Library
# 通常位於 lib/msvc (若是 64-bit 則通常在 lib64/msvc)
link_directories("${NI_DAQ_ROOT}/lib/msvc")

# --- 來源檔案設定 ---
# 為了保持乾淨，我們使用 file(GLOB...) 暫時抓取 src, src/daq 下所有 cpp，
# 後續模組變多時，建議明確列出檔案。
file(GLOB SOURCES "src/*.cpp" "src/daq/*.cpp")

# --- 產生執行檔 ---
add_executable(${PROJECT_NAME} ${SOURCES})

# --- 連結 Library ---
# GCC/MinGW 可以直接連結 MSVC 格式的 .lib 檔案 (如果是 C 介面)
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    "${NI_DAQ_ROOT}/lib/msvc/NIDAQmx.lib"
)

# --- 輸出訊息 (方便 Debug 路徑是否正確) ---
message(STATUS "NI-DAQmx Root: ${NI_DAQ_ROOT}")
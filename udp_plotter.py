import socket
import matplotlib.pyplot as plt
import numpy as np
from collections import deque
import time

# ================= 設定區 =================
UDP_IP = "127.0.0.1"
UDP_PORT = 5005
BUFFER_SIZE = 65535      # 加大 Buffer 以接收長字串
TIME_WINDOW = 5.0        # 顯示最近 5 秒的數據
REFRESH_INTERVAL = 0.05  # 繪圖刷新間隔 (秒)，避免過度消耗 CPU

# 定義任務名稱關鍵字對應的圖表位置 (Row Index)
# 您的 Task Name 必須包含這些關鍵字
TASK_MAPPING = {
    "HighSpeed": 0,  # 第一列
    "MediumSpeed": 1, # 第二列
    "LowSpeed": 2    # 第三列
}
# ==========================================

class RealTimePlotter:
    def __init__(self):
        # 初始化 UDP Socket
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((UDP_IP, UDP_PORT))
        self.sock.setblocking(False) # 設定為非阻塞模式
        print(f"Listening on {UDP_IP}:{UDP_PORT}...")

        # 初始化繪圖視窗 (3 列 1 行)
        plt.ion() # 開啟互動模式
        self.fig, self.axes = plt.subplots(3, 1, figsize=(10, 12), sharex=False)
        self.fig.canvas.manager.set_window_title('Distributed DAQ System Monitor')
        
        # 設定標題與格式
        titles = ["High Speed Task (50kHz)", "Medium Speed Task (5kHz)", "Low Speed Task (10Hz)"]
        for i, ax in enumerate(self.axes):
            ax.set_title(titles[i])
            ax.set_ylabel("Voltage / Unit")
            ax.grid(True, linestyle='--', alpha=0.6)
            ax.set_xlim(0, TIME_WINDOW)
            
        self.axes[2].set_xlabel("Time History (sec)")

        # 資料緩衝區結構: 
        # self.data_store[task_name][channel_index] = deque(...)
        self.data_store = {} 
        
        # 線條物件快取: 
        # self.lines[task_name][channel_index] = line_object
        self.lines = {}

        # 顏色池 (用於區分不同通道)
        self.colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd', '#8c564b', '#e377c2', '#7f7f7f']

    def parse_packet(self, raw_str):
        """ 解析 CSV 封包 """
        try:
            # 格式: Name, Timestamp, Rate, ChCount, TotalPoints, Data...
            parts = raw_str.split(',')
            if len(parts) < 6: return None

            name = parts[0]
            # timestamp = int(parts[1]) # 暫時不用
            rate = float(parts[2])
            ch_count = int(parts[3])
            # total_points = int(parts[4])
            data_values = [float(x) for x in parts[5:]]

            return name, rate, ch_count, data_values
        except Exception as e:
            print(f"Error parsing packet: {e}")
            return None

    def update(self):
        try:
            # 1. 讀取所有積壓在 Socket 的封包 (避免延遲)
            while True:
                try:
                    data, _ = self.sock.recvfrom(BUFFER_SIZE)
                    packet = self.parse_packet(data.decode('utf-8'))
                    
                    if packet:
                        self.process_data(*packet)
                except BlockingIOError:
                    break # 已無資料
        except KeyboardInterrupt:
            return False

        # 2. 更新圖表
        self.draw_plots()
        
        # 3. 處理 GUI 事件
        self.fig.canvas.flush_events()
        return True

    def process_data(self, name, rate, ch_count, data_values):
        """ 將數據存入對應的 Buffer """
        
        # 若是新任務，初始化儲存空間
        if name not in self.data_store:
            # 計算 Buffer 長度: 時間視窗 * 取樣率 (但因為 UDP 是降樣傳輸的，這裡僅需估算顯示點數)
            # 為了簡單，我們假設 UDP 每秒送約 1000 點 (10次 * 100點)
            max_len = int(1000 * TIME_WINDOW) 
            self.data_store[name] = [deque(maxlen=max_len) for _ in range(ch_count)]
            self.lines[name] = [None] * ch_count

        # 解交錯 (De-interleave) 並存入 Buffer
        # 數據排列: ch0, ch1, ch2, ch0, ch1, ch2...
        num_points = len(data_values) // ch_count
        
        for i in range(num_points):
            for ch in range(ch_count):
                val = data_values[i * ch_count + ch]
                self.data_store[name][ch].append(val)

    def draw_plots(self):
        """ 繪製線條 """
        for name, channels_data in self.data_store.items():
            
            # 決定要在哪一個 subplot 繪圖
            ax_idx = -1
            for key, idx in TASK_MAPPING.items():
                if key in name:
                    ax_idx = idx
                    break
            
            if ax_idx == -1: continue # 找不到對應的圖表位置

            ax = self.axes[ax_idx]
            
            # 繪製每個通道
            for ch_idx, data_deque in enumerate(channels_data):
                if len(data_deque) < 2: continue

                y_data = list(data_deque)
                x_data = np.linspace(0, TIME_WINDOW, len(y_data)) # 簡單生成 X 軸 (0 ~ 5秒)

                # 若線條尚未建立，則建立之
                if self.lines[name][ch_idx] is None:
                    line, = ax.plot(x_data, y_data, 
                                    label=f"{name}-Ch{ch_idx}", 
                                    color=self.colors[ch_idx % len(self.colors)],
                                    linewidth=1.0)
                    self.lines[name][ch_idx] = line
                    ax.legend(loc='upper right', fontsize='small', framealpha=0.5)
                else:
                    # 更新數據
                    self.lines[name][ch_idx].set_ydata(y_data)
                    self.lines[name][ch_idx].set_xdata(x_data)

            # 自動調整 Y 軸 (可選，若覺得跳動太快可註解掉)
            # ax.relim()
            # ax.autoscale_view(scalex=False, scaley=True)

if __name__ == "__main__":
    plotter = RealTimePlotter()
    
    print("Plotter running... Press Ctrl+C to stop.")
    try:
        while True:
            if not plotter.update():
                break
            time.sleep(REFRESH_INTERVAL)
    except KeyboardInterrupt:
        pass
    finally:
        plotter.sock.close()
        print("Closed.")
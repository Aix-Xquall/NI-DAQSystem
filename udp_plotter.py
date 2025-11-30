import socket
import json
import time
import sys
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import RadioButtons
from collections import deque

# ================= 設定區 =================
CONFIG_FILE = "DAQ_Settings.json"
UDP_IP = "127.0.0.1"
UDP_PORT = 5005
BUFFER_SIZE = 65536      # 64KB Buffer
REFRESH_INTERVAL = 0.05  # 刷新率 20 FPS (降低 CPU 使用率)
MAX_POINTS = 20000       # 加大緩衝區，確保能容納 10s 以上的數據
# ==========================================

class SystemMapper:
    """
    負責解析 JSON 設定檔，建立「UDP 封包數據」到「繪圖視窗」的映射關係。
    """
    def __init__(self, config_file):
        self.plots_info = [] # 儲存 8 個 Slot 的標題資訊
        self.task_map = {}   # { "TaskName": [ (SlotIndex, LineLabel), ... ] }
        self.load_config(config_file)

    def load_config(self, path):
        try:
            with open(path, 'r', encoding='utf-8') as f:
                config = json.load(f)
            
            print(f"[System] Loading Config: {config.get('system_name')}")
            
            # 1. 準備 8 個繪圖區的資訊 (依序對應 Slot 1~8)
            slot_counter = 0
            
            # 2. 遍歷 Tasks
            for task in config.get('tasks', []):
                if not task.get('active', False):
                    continue
                
                task_name = task['task_name']
                self.task_map[task_name] = []
                
                # 3. 遍歷 Channels (Devices)
                for ch in task.get('channels', []):
                    if not ch.get('active', True):
                        continue
                    
                    # 取得裝置資訊
                    dev_name = ch['device_name']
                    model_info = ch.get('model_info', dev_name)
                    
                    # 分配 Slot ID (假設 JSON 順序即為 Slot 順序)
                    current_slot_id = slot_counter
                    self.plots_info.append(f"Slot {current_slot_id+1}: {model_info}")
                    slot_counter += 1
                    
                    # 解析通道數量 (例如 ai0:1 代表 2 個通道)
                    range_str = ch['channel_range']
                    if ':' in range_str:
                        start, end = range_str.replace('ai', '').split(':')
                        num_ch = int(end) - int(start) + 1
                    else:
                        num_ch = 1
                    
                    # 建立映射: 告訴程式這個 Task 接下來的 num_ch 個數據屬於 current_slot_id
                    for i in range(num_ch):
                        label = f"Ch{i}" 
                        self.task_map[task_name].append((current_slot_id, label))
            
            print(f"[System] Configured {len(self.plots_info)} slots.")
            
        except Exception as e:
            print(f"[Error] Config load failed: {e}")
            sys.exit(1)

class RealTimePlotter:
    def __init__(self, mapper):
        self.mapper = mapper
        self.running = True
        self.time_window = 3.0 # 預設 3 秒
        
        # --- 1. 初始化 UDP ---
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((UDP_IP, UDP_PORT))
        self.sock.setblocking(False) # 非阻塞模式
        print(f"[UDP] Listening on {UDP_IP}:{UDP_PORT}...")

        # --- 2. 初始化資料結構 ---
        # data_buffers[slot_id][ch_index] = deque()
        self.data_buffers = []
        # 儲存每個 Slot 的「有效取樣率」 (Effective Sample Rate)
        self.slot_rates = {} 
        
        for _ in range(len(self.mapper.plots_info)):
            self.data_buffers.append({}) # 每個 Slot 一個字典存放通道數據

        # --- 3. 初始化 GUI ---
        plt.ion()
        # 建立 4x2 網格
        self.fig, self.axes = plt.subplots(4, 2, figsize=(16, 10))
        self.axes = self.axes.flatten()
        
        plt.subplots_adjust(left=0.08, bottom=0.05, right=0.98, top=0.90, wspace=0.15, hspace=0.4)
        self.fig.canvas.manager.set_window_title('NI cDAQ-9189 Real-Time Monitor')

        self.lines = {} # 儲存 Line2D 物件
        
        for i, ax in enumerate(self.axes):
            if i < len(self.mapper.plots_info):
                ax.set_title(self.mapper.plots_info[i], fontsize=9, fontweight='bold')
                ax.grid(True, linestyle=':', alpha=0.6)
                ax.set_xlim(0, self.time_window)
                self.lines[i] = {}
            else:
                ax.axis('off')

        # --- 4. 加入控制項 (時間選擇) ---
        ax_radio = plt.axes([0.02, 0.92, 0.15, 0.06], facecolor='#e6e6e6')
        self.radio = RadioButtons(ax_radio, ('3s', '5s', '10s'), active=0)
        self.radio.on_clicked(self.change_time_window)
        
        self.fig.canvas.mpl_connect('key_press_event', self.on_key)
        self.fig.canvas.mpl_connect('close_event', self.on_close)

    def change_time_window(self, label):
        """ 切換時間顯示範圍 """
        self.time_window = float(label.replace('s', ''))
        # 立即更新所有圖表的 X 軸範圍
        for ax in self.axes:
            if ax.get_visible():
                ax.set_xlim(0, self.time_window)
        print(f"[UI] Time window set to {self.time_window}s")

    def parse_and_store(self, raw_data):
        """ 解析 UDP 封包並分發數據 """
        try:
            msg = raw_data.decode('utf-8').strip()
            parts = msg.split(',')
            if len(parts) < 6: return

            task_name = parts[0]
            # rate = float(parts[2]) 
            packet_ch_count = int(parts[3])
            data_values = [float(x) for x in parts[5:]]

            if task_name not in self.mapper.task_map: return
            mapping = self.mapper.task_map[task_name]

            # 計算 Scans (時間點數量)
            num_scans = len(data_values) // packet_ch_count
            if num_scans == 0: return

            # === 關鍵修正：計算有效取樣率 ===
            # C++ 每 0.1 秒發送一次封包。因此：有效率 = 此封包點數 * 10
            # 這是唯一能正確對應真實時間的方法
            effective_rate = num_scans * 10.0

            for ch_idx in range(packet_ch_count):
                if ch_idx >= len(mapping): break
                
                slot_id, label = mapping[ch_idx]
                
                # 更新該 Slot 的速率資訊
                self.slot_rates[slot_id] = effective_rate

                ch_data = data_values[ch_idx::packet_ch_count]
                
                if ch_idx not in self.data_buffers[slot_id]:
                    self.data_buffers[slot_id][ch_idx] = deque(maxlen=MAX_POINTS)
                
                self.data_buffers[slot_id][ch_idx].extend(ch_data)

        except Exception as e:
            pass

    def update(self):
        """ 主更新迴圈 """
        # 1. 讀取 Socket 所有數據
        while True:
            try:
                data, _ = self.sock.recvfrom(BUFFER_SIZE)
                self.parse_and_store(data)
            except BlockingIOError:
                break

        # 2. 更新繪圖
        for slot_id, channels in enumerate(self.data_buffers):
            if slot_id >= len(self.axes): break
            ax = self.axes[slot_id]
            
            # 取得該 Slot 的有效取樣率 (預設 10Hz 以防未收到數據)
            eff_rate = self.slot_rates.get(slot_id, 10.0)
            if eff_rate <= 0: eff_rate = 1.0

            # 計算當前視窗需要多少點
            points_needed = int(self.time_window * eff_rate)

            # 遍歷通道
            for ch_idx, data_deque in channels.items():
                if len(data_deque) < 2: continue
                
                # === 關鍵修正：只取出對應時間長度的數據 ===
                # 將 deque 轉為 list (這步在 Python 很快)
                full_data = list(data_deque)
                
                # 根據需要的點數進行 Slice (切片)
                # 如果緩衝區夠大，只取最後 points_needed 點
                # 如果緩衝區不夠，就全取
                if len(full_data) > points_needed:
                    y_data = full_data[-points_needed:]
                else:
                    y_data = full_data

                # 生成 X 軸
                # 讓數據靠右對齊 (最新的數據在 time_window)
                # 時間長度 = 點數 / 速率
                duration = len(y_data) / eff_rate
                x_data = np.linspace(self.time_window - duration, self.time_window, len(y_data))

                # 繪圖
                if ch_idx not in self.lines[slot_id]:
                    line, = ax.plot(x_data, y_data, linewidth=1.0, label=f"Ch{ch_idx}")
                    self.lines[slot_id][ch_idx] = line
                    ax.legend(loc='upper right', fontsize=6, framealpha=0.5)
                else:
                    self.lines[slot_id][ch_idx].set_data(x_data, y_data)
            
            # 限制自動縮放頻率或範圍
            ax.relim()
            ax.autoscale_view(scalex=False, scaley=True)

        self.fig.canvas.flush_events()

    def on_key(self, event):
        if event.key == 'enter':
            print("[UI] Enter pressed. Exiting...")
            self.running = False

    def on_close(self, event):
        print("[UI] Window closed.")
        self.running = False

if __name__ == "__main__":
    print("=== NI cDAQ-9189 Monitor (Fixed Time Scaling) ===")
    mapper = SystemMapper(CONFIG_FILE)
    plotter = RealTimePlotter(mapper)
    
    print("Starting... Press 'Enter' or Ctrl+C to stop.")
    try:
        while plotter.running:
            plotter.update()
            time.sleep(REFRESH_INTERVAL)
    except KeyboardInterrupt:
        print("\n[UI] Ctrl+C detected.")
    finally:
        plotter.sock.close()
        plt.close('all')
        print("Closed.")
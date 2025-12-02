import socket
import json
import threading
import queue
import time
import sys
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Button
from collections import deque

# ================= 設定區 =================
CONFIG_FILE = "DAQ_Settings.json"
UDP_IP = "0.0.0.0"       # 監聽所有網卡
UDP_PORT = 5005
BUFFER_SIZE = 65536      # 64KB UDP Buffer
MAX_FPS = 30             # UI 更新率上限
PLOT_DISPLAY_LIMIT = 50000 # 繪圖優化：畫面上最多只顯示 50000 點
MAX_BUFFER_SEC = 20.0    # 記憶體中保留最長 20 秒的歷史數據
# ==========================================

class SystemMapper:
    """
    負責解析 JSON 設定檔:
    1. 建立 Device Name -> Slot Index 的映射
    2. 計算每個 Slot 的「有效頻率 (Effective Rate)」
    """
    def __init__(self, config_path):
        self.slot_titles = [] 
        # device_map: { "DeviceName": SlotIndex }
        self.device_map = {} 
        # slot_rates: { SlotIndex: EffectiveRate(Hz) }
        self.slot_rates = {}
        
        self.load_config(config_path)

    def load_config(self, path):
        try:
            with open(path, 'r', encoding='utf-8') as f:
                config = json.load(f)
            
            print(f"[System] Loading Config: {config.get('system_name')}")
            print("-" * 60)
            print(f"{'Slot':<8} {'Device':<12} {'TaskRate':<10} {'Window':<8} {'Eff. Rate':<10}")
            print("-" * 60)
            
            # 建立 Slot 列表 (假設 JSON 順序即為 Slot 1~8)
            for task in config.get('tasks', []):
                if not task.get('active', False):
                    continue
                
                task_rate = float(task.get('sample_rate', 1000.0))
                
                for ch in task.get('channels', []):
                    if not ch.get('active', True):
                        continue
                    
                    dev_name = ch.get('device_name')
                    model = ch.get('model_info', dev_name)
                    
                    # 取得降頻參數
                    avg_cfg = ch.get('moving_avg', {})
                    window_size = 1
                    if avg_cfg.get('active', False):
                        window_size = int(avg_cfg.get('window_size', 1))
                    
                    # 計算有效頻率
                    eff_rate = task_rate / window_size

                    # 註冊 Slot (若尚未註冊)
                    if dev_name not in self.device_map:
                        self.slot_titles.append(f"Slot {len(self.slot_titles)+1}: {model}")
                        current_slot_idx = len(self.slot_titles) - 1
                        self.device_map[dev_name] = current_slot_idx
                        self.slot_rates[current_slot_idx] = eff_rate # 初始頻率
                    else:
                        # 若 Slot 已存在 (例如 Slot 8 有 A/B 兩組)，取較高的頻率作為主頻率
                        current_slot_idx = self.device_map[dev_name]
                        if eff_rate > self.slot_rates[current_slot_idx]:
                            self.slot_rates[current_slot_idx] = eff_rate

                    # 列印表格資訊
                    print(f"{current_slot_idx+1:<8} {dev_name:<12} {task_rate:<10.1f} {window_size:<8} {eff_rate:<10.2f} Hz")

            print("-" * 60)
            print(f"[System] Configured {len(self.slot_titles)} slots.")
            
        except Exception as e:
            print(f"[Error] Config load failed: {e}")
            sys.exit(1)

class RealTimePlotter:
    def __init__(self, mapper):
        self.mapper = mapper
        self.running = True
        
        # Queue 用於 Thread 間通訊
        self.packet_queue = queue.Queue()
        
        # UI 狀態 - 預設 100ms (0.1s)
        self.time_window = 0.1 
        
        # 資料儲存結構: buffers[slot_idx][ch_index] = deque()
        self.buffers = [{} for _ in range(len(self.mapper.slot_titles))]
        
        # 計算每個 Slot 的最大 Buffer 長度
        self.slot_max_lens = {}
        for slot_idx, rate in self.mapper.slot_rates.items():
            self.slot_max_lens[slot_idx] = int(rate * MAX_BUFFER_SEC) + 100

        # 繪圖物件快取
        self.lines = [{} for _ in range(len(self.mapper.slot_titles))]
        
        # 按鈕物件快取
        self.btns = []
        self.btn_axes = []
        
        self.init_plot()
        
        # 啟動 UDP 執行緒
        self.udp_thread = threading.Thread(target=self.udp_worker, daemon=True)
        self.udp_thread.start()

    def init_plot(self):
        plt.ion() 
        self.fig, self.axes = plt.subplots(4, 2, figsize=(16, 10))
        self.axes = self.axes.flatten()
        
        self.fig.canvas.manager.set_window_title('NI cDAQ-9189 Monitor (Auto-Rate & Downsampling)')
        plt.subplots_adjust(left=0.06, bottom=0.05, right=0.98, top=0.92, wspace=0.15, hspace=0.35)

        # 設定 Slot 標題與軸
        for i, ax in enumerate(self.axes):
            if i < len(self.mapper.slot_titles):
                title = self.mapper.slot_titles[i]
                rate = self.mapper.slot_rates.get(i, 0)
                # 標題加入頻率資訊
                ax.set_title(f"{title} (Rate: {rate:.1f}Hz)", fontsize=9, fontweight='bold', pad=3)
                ax.set_xlabel("Time (s)", fontsize=7)
                ax.grid(True, linestyle=':', alpha=0.7)
                ax.tick_params(labelsize=7)
                ax.set_xlim(-self.time_window, 0)
            else:
                ax.set_visible(False)

        # --- 時間選擇按鈕 (上方水平排列) ---
        labels = ['10ms', '100ms', '1000ms', '5S', '10S']
        
        n_btns = len(labels)
        btn_w = 0.08  # 按鈕寬度
        btn_h = 0.04  # 按鈕高度
        gap = 0.01    # 間距
        
        total_w = n_btns * btn_w + (n_btns - 1) * gap
        start_x = 0.5 - (total_w / 2)
        y_pos = 0.94
        
        self.btns = []
        self.btn_axes = []

        for i, label in enumerate(labels):
            x = start_x + i * (btn_w + gap)
            ax_btn = plt.axes([x, y_pos, btn_w, btn_h])
            
            # 建立按鈕
            btn = Button(ax_btn, label, color='0.9', hovercolor='0.8')
            
            # [修改] 預設選中 100ms，顯示為橘色
            if label == '100ms':
                btn.color = 'orange'
                ax_btn.set_facecolor('orange')
            
            # 綁定點擊事件
            btn.on_clicked(lambda event, l=label, b=btn: self.change_window(l, b))
            
            self.btns.append(btn)
            self.btn_axes.append(ax_btn)

        self.fig.canvas.mpl_connect('key_press_event', self.on_key)
        self.fig.canvas.mpl_connect('close_event', self.on_close)

    def change_window(self, label, clicked_btn):
        # 解析時間標籤
        label_lower = label.lower()
        val = 0.1
        
        if 'ms' in label_lower:
            val = float(label_lower.replace('ms', '')) / 1000.0
        elif 's' in label_lower:
            val = float(label_lower.replace('s', ''))
        
        self.time_window = val
        print(f"[UI] Time Window set to: {self.time_window}s")
        
        # [修改] 更新按鈕視覺狀態：選中者橘色，其餘灰色
        for btn in self.btns:
            if btn == clicked_btn:
                btn.color = 'orange'
                btn.ax.set_facecolor('orange')
            else:
                btn.color = '0.9'
                btn.ax.set_facecolor('0.9')
        
        self.fig.canvas.draw_idle()
        
        # 立即更新所有圖表的 X 軸範圍
        for ax in self.axes:
            if ax.get_visible():
                ax.set_xlim(-self.time_window, 0)

    def udp_worker(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((UDP_IP, UDP_PORT))
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024) # 2MB Buffer
        print(f"[UDP] Listening on port {UDP_PORT}...")

        while self.running:
            try:
                data, _ = sock.recvfrom(BUFFER_SIZE)
                self.packet_queue.put(data)
            except OSError:
                break
        sock.close()

    def process_packet(self, raw_data):
        try:
            msg = raw_data.decode('utf-8').strip()
            parts = msg.split(',')
            
            # Protocol: HeaderName, Timestamp, Rate, ChCount, Points, Data...
            if len(parts) < 6: return

            header_name = parts[0]
            
            # 識別 Slot
            target_slot = -1
            for dev_name, slot_idx in self.mapper.device_map.items():
                if header_name.startswith(dev_name):
                    target_slot = slot_idx
                    break
            
            if target_slot == -1: return 

            ch_count = int(parts[3])
            data_values = [float(x) for x in parts[5:]]
            
            # 根據預先計算的頻率決定 Buffer 長度
            maxlen = self.slot_max_lens.get(target_slot, 5000)

            for i in range(ch_count):
                if i >= len(data_values): break
                val = data_values[i]
                
                if i not in self.buffers[target_slot]:
                    self.buffers[target_slot][i] = deque(maxlen=maxlen)
                
                self.buffers[target_slot][i].append(val)

        except Exception:
            pass

    def update_plot(self):
        while self.running:
            start_time = time.time()

            # 1. 處理所有堆積的封包
            while not self.packet_queue.empty():
                self.process_packet(self.packet_queue.get())

            # 2. 更新圖表
            for slot_idx, ax in enumerate(self.axes):
                if slot_idx >= len(self.buffers): break
                
                slot_data = self.buffers[slot_idx]
                if not slot_data: continue

                # 取得該 Slot 的有效頻率
                eff_rate = self.mapper.slot_rates.get(slot_idx, 10.0)
                
                # 計算「當前時間視窗」需要顯示多少點
                points_needed = int(eff_rate * self.time_window)
                
                # 若計算出的點數太少 (剛啟動時或視窗極小)，至少給一點緩衝
                if points_needed < 10: points_needed = 10

                has_update = False
                
                for ch_idx, data_deque in slot_data.items():
                    if len(data_deque) < 2: continue
                    has_update = True
                    
                    # --- 關鍵優化：智慧取樣 (Downsampling) ---
                    full_data = list(data_deque)
                    
                    # 只取出需要的歷史長度
                    if len(full_data) > points_needed:
                        display_data = full_data[-points_needed:]
                    else:
                        display_data = full_data
                    
                    # 檢查是否超過顯示上限，進行降頻
                    num_points = len(display_data)
                    step = 1
                    if num_points > PLOT_DISPLAY_LIMIT:
                        step = num_points // PLOT_DISPLAY_LIMIT + 1
                        display_data = display_data[::step]
                        num_points = len(display_data)

                    # 生成對應的 X 軸
                    actual_duration = (num_points * step) / eff_rate
                    x_data = np.linspace(-actual_duration, 0, num_points)
                    
                    # 繪圖
                    label = f"Ch{ch_idx}"
                    if ch_idx not in self.lines[slot_idx]:
                        line, = ax.plot([], [], label=label, linewidth=1.2 if eff_rate < 100 else 1.0)
                        self.lines[slot_idx][ch_idx] = line
                        if ch_idx == 0: 
                            ax.legend(loc='upper left', fontsize=6, framealpha=0.5)

                    self.lines[slot_idx][ch_idx].set_data(x_data, display_data)

                if has_update:
                    ax.set_xlim(-self.time_window, 0)
                    ax.relim()
                    ax.autoscale_view(scalex=False, scaley=True)

            self.fig.canvas.flush_events()
            
            # 控速
            elapsed = time.time() - start_time
            sleep_time = (1.0 / MAX_FPS) - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    def on_key(self, event):
        if event.key == 'enter':
            self.close()

    def on_close(self, event):
        self.close()

    def close(self):
        self.running = False
        plt.close(self.fig)
        sys.exit(0)

if __name__ == "__main__":
    print("=== UDP Plotter (Auto-Configured Rate) ===")
    mapper = SystemMapper(CONFIG_FILE)
    plotter = RealTimePlotter(mapper)
    
    print("\nRunning...")
    try:
        plotter.update_plot()
    except KeyboardInterrupt:
        plotter.close()
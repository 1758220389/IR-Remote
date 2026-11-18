"""Read-only monitoring and operator annotation for the formal remote."""
from datetime import datetime
import json
from pathlib import Path
from queue import Empty, SimpleQueue
from struct import Struct
from threading import Event, Thread
import tkinter as tk
from tkinter import ttk

import capture_swd as swd
from recognition_swd import FLAGS, LABELS
from windows import durable

WIRE = Struct("<16I4f")
PROTOCOL = 0x53484431
CONTROL_PROTOCOL = 0x53484432
MODEL = 0x00010000
OUT = Path(__file__).resolve().parents[1] / "data/shadow"
REASONS = {**FLAGS, 32: "I²C 失败", 64: "实际采样间隔异常"}
ACTIONS = {0: "无", 1: "BUTTON1 · 升温", 2: "BUTTON2 · 降温"}
TRUTH = ("CW", "CCW", "NONE 静置", "NONE 非目标", "误触", "长按截断")


def read_snapshot(target, address):
    sample = WIRE.unpack(bytes(target.read_memory_block8(address, WIRE.size)))
    if sample[0] not in (PROTOCOL, CONTROL_PROTOCOL) or sample[1] != MODEL:
        raise RuntimeError("需要正式影子版固件与匹配的 IR_Remote.map")
    if sample[2] & 1 or target.read32(address + 8) != sample[2]:
        return None
    return sample


def collect(args, stop, queue):
    try:
        address = swd.capture_address(args.map, "g_gesture_shadow")
    except (OSError, RuntimeError) as error:
        queue.put(("offline", str(error)))
        return
    while not stop.is_set():
        try:
            session = swd.connect_probe(args.pack, frequency=args.swd_hz)
            if session is None:
                raise ConnectionError("未发现 ST-Link")
            with session:
                while not stop.is_set():
                    sample = read_snapshot(session.target, address)
                    if sample:
                        queue.put(("snapshot", sample))
                    stop.wait(.15)
        except (RuntimeError, ConnectionError) as error:
            queue.put(("offline", f"正在重连 · {error}"))
            stop.wait(2)


class ShadowApp:
    def __init__(self, root, args):
        self.root, self.stop, self.queue = root, Event(), SimpleQueue()
        self.current = None
        self.annotated = None
        self.saved = None
        self.current_id = None
        self.record_id = 0
        self.last_number = 0
        self.epoch = 1
        OUT.mkdir(parents=True, exist_ok=True)
        self.path = OUT / datetime.now().strftime("shadow-%Y%m%d-%H%M%S-%f.jsonl")
        root.title("正式遥控器 · 手势监测")
        root.geometry("790x460")
        frame = ttk.Frame(root, padding=20)
        frame.pack(fill="both", expand=True)
        self.status = tk.StringVar(value="正在连接 ST-Link")
        self.result = tk.StringVar(value="等待完整窗口")
        self.timing = tk.StringVar()
        self.reason = tk.StringVar()
        self.note = tk.StringVar()
        self.truth = tk.StringVar()
        ttk.Label(frame, text="手势监测", font=("Microsoft YaHei UI", 20, "bold")).pack(anchor="w")
        for variable in (self.status, self.result, self.timing, self.reason):
            ttk.Label(frame, textvariable=variable, wraplength=730).pack(anchor="w", pady=6)
        ttk.Separator(frame).pack(fill="x", pady=8)
        row = ttk.Frame(frame)
        row.pack(fill="x")
        ttk.Label(row, text="实际动作").pack(side="left", padx=(0, 8))
        ttk.Combobox(row, textvariable=self.truth, values=TRUTH, state="readonly", width=17).pack(side="left")
        ttk.Button(row, text="补标注实际动作", command=self.save).pack(side="left", padx=8)
        ttk.Label(frame, textvariable=self.note, wraplength=730).pack(anchor="w", pady=8)
        self.worker = Thread(target=collect, args=(args, self.stop, self.queue))
        self.worker.start()
        root.after(100, self.update)
        root.protocol("WM_DELETE_WINDOW", self.close)

    def update(self):
        try:
            while True:
                event, value = self.queue.get_nowait()
                if event == "offline":
                    self.status.set(value)
                else:
                    self.show(value)
        except Empty:
            pass
        finally:
            if not self.stop.is_set():
                self.root.after(100, self.update)

    def show(self, s):
        _, _, seq, number, who, sensor, button, flags, count, duration, key, label, low, high, late, total, angle, *scores = s
        if number < self.last_number:
            self.epoch += 1
        self.last_number = number
        mode = "已启用控制" if s[0] == CONTROL_PROTOCOL else "影子验证"
        self.status.set(f"ST-Link 已连接 · {mode} · WHO_AM_I 0x{who:02X} · " +
                        ("传感器未就绪" if sensor != 2 else
                         {0: "等待松开", 1: "可以按 BT6", 2: "正在采集"}.get(button, "未知状态")))
        if not number:
            if self.current is None:
                self.result.set("等待完整窗口")
            return
        snapshot_id = (self.epoch, s[2:4], s[7:])
        if snapshot_id != self.saved:
            record = self.window_record(s)
            try:
                self.append({"type": "window", "record_id": self.record_id + 1, **record})
            except OSError as error:
                self.note.set(f"窗口暂未落盘：{error}")
            else:
                self.saved = snapshot_id
                self.current = s
                self.record_id += 1
                self.current_id = self.record_id
                self.note.set(f"#{number} 已自动保存，可补标注 · {self.path.name}")
        action = "按键事件" if s[0] == CONTROL_PROTOCOL else "拟议"
        self.result.set(f"#{number} · MCU {LABELS[label]} · {action} {ACTIONS[key]} · Z {angle:+.3f}° · 累计 {total}")
        self.timing.set(f"{count} 点 / {duration} ms · 实际间隔 {low}–{high} µs · 跳过 {late} 个周期")
        self.reason.set("拒绝 · " + " / ".join(text for bit,text in REASONS.items() if flags & bit) if flags
                        else "分数 CCW / CW / NONE · " + " / ".join(f"{score:+.4f}" for score in scores))

    def append(self, record):
        with self.path.open("a", encoding="utf-8") as log:
            log.write(json.dumps(record, ensure_ascii=False) + "\n")
            durable(log)

    def window_record(self, s):
        return dict(time=datetime.now().astimezone().isoformat(), epoch=self.epoch, window=s[3],
                    mcu=LABELS[s[11]], action_key=s[10], control_enabled=s[0] == CONTROL_PROTOCOL,
                    flags=s[7], count=s[8],
                    duration_ms=s[9], gap_min_us=s[12], gap_max_us=s[13],
                    missed_periods=s[14], angle_deg=s[16], scores=s[17:20])

    def save(self):
        if self.current is None or self.truth.get() not in TRUTH:
            self.note.set("请先等窗口自动保存，再选择刚才的实际动作。")
            return
        if self.annotated == self.current_id:
            self.note.set("本窗口已标注。")
            return
        number = self.current[3]
        try:
            self.append(dict(type="annotation", time=datetime.now().astimezone().isoformat(),
                             record_id=self.current_id, window=number, actual=self.truth.get()))
        except OSError as error:
            self.note.set(f"标注未保存：{error}")
            return
        self.annotated = self.current_id
        self.note.set(f"已标注 #{number} · {self.path.name}")
        self.truth.set("")

    def close(self):
        self.stop.set()
        if self.worker.is_alive():
            self.root.after(80, self.close)
        else:
            self.root.destroy()

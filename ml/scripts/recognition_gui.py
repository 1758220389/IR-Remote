"""Read actual MCU predictions and compare the exact SWD snapshot on PC."""
import json
from pathlib import Path
from queue import Empty, SimpleQueue
from threading import Event, Thread
import sys
import tkinter as tk
from tkinter import ttk

import joblib
import numpy as np
import pandas as pd

import capture_swd as swd
import recognition_swd as protocol
from windows import physical

ML = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ML))
from analysis.features import extract


def run(args, stop, events):
    try:
        folder = ML / "models/v1"
        details = json.loads((folder / "recommended_C.json").read_text())
        artifact = joblib.load(folder / "recommended_C.joblib")
        if json.loads(json.dumps(artifact["details"])) != details:
            raise ValueError("PC 模型与参数文件不一致")
        model = artifact["model"]
        address = protocol.address_for(args.map)
        pending = swd.capture_address(args.map, "g_pending_window")
        capture = swd.capture_address(args.map, "g_swd_capture")
        while not stop.is_set():
            try:
                session = swd.connect_probe(args.pack, frequency=args.swd_hz)
                if session is None:
                    raise ConnectionError("未找到 ST-Link")
                with session:
                    seen = None
                    while not stop.is_set():
                        _, _, sensor, fault, button, *_ = swd.read_header(session.target, capture)
                        state = protocol.header(session.target, address)
                        if protocol.request_mode(session.target, address, state, protocol.RECOGNIZE):
                            sensor_name = swd.SENSORS.get(sensor, "传感器待识别")
                            events.put(("status", f"ST-Link 已连接 · {sensor_name} · 100 Hz · " +
                                        (f"传感器异常 {fault}" if fault else
                                         ("正在采集" if button == 1 else
                                          "等待松开" if button == 2 else "可按 BT6"))))
                            if state[4] and state[3:5] != seen:
                                rows = protocol.read_snapshot(session.target, address, pending, state)
                                if rows is not None:
                                    comparison = compare(state, rows, model, details)
                                    events.put(("result", (state, rows, comparison)))
                                    seen = state[3:5]
                            elif not state[4]:
                                seen = None
                        else:
                            events.put(("status", "等待板端切换模式。请松开 BT6，采集缓存需先保存确认。"))
                        stop.wait(.08)
            except (RuntimeError, ConnectionError) as error:
                events.put(("status", f"ST-Link 正在重连 · {error}"))
                stop.wait(2)
    except (OSError, ValueError, RuntimeError) as error:
        events.put(("status", f"识别后端无法启动 · {error}"))


def compare(state, rows, model, details):
    _, _, _, _, _, flags, count, ms, _, _, *_ = state
    # Reject independently using the snapshot's duration, cutoff flag and raw rows.
    cutoff = bool(flags & 2)
    expected = int(ms < 150) | (2 if cutoff else 0)
    if count < 2 or count > 150 or ms > 1500 or abs(count * 10 - ms) >= 10:
        expected |= 4
    if not expected:
        if any(((b[0] - a[0]) & 0xFFFFFFFF) != 10 for a, b in zip(rows, rows[1:])):
            expected |= 8
        if any(value in (-32768, 32767) for row in rows for value in row[1:]):
            expected |= 16
    if expected:
        return expected, None
    values = np.asarray(rows)
    features = extract(values[:, 0], values[:, 1:4] / 16384, values[:, 4:] / 131,
                       details["bias_dps"], ms)
    angle = features["gyro_integral_z_deg"]
    x = pd.DataFrame({"gyro_integral_z_deg": [angle]})
    return expected, (model.predict(x)[0], angle, model.decision_function(x)[0])


class RecognitionApp:
    def __init__(self, root, args, waveform):
        self.root, self.stop, self.events = root, Event(), SimpleQueue()
        root.title("BT6 实时识别 · STM32 / PC 对照")
        root.geometry("930x700")
        root.minsize(740, 550)
        frame = ttk.Frame(root, padding=16)
        frame.pack(fill="both", expand=True)
        self.status = tk.StringVar(value="正在连接 ST-Link")
        self.window = tk.StringVar(value="等待板端结果")
        self.mcu = tk.StringVar(value="MCU · —")
        self.pc = tk.StringVar(value="PC 对照 · —")
        self.match = tk.StringVar(value="")
        ttk.Label(frame, text="STM32 手势识别", font=("Microsoft YaHei UI", 20, "bold")).pack(anchor="w")
        for variable in (self.status, self.window, self.mcu, self.pc, self.match):
            ttk.Label(frame, textvariable=variable, wraplength=690).pack(anchor="w", pady=4)
        ttk.Label(frame, text="按下 BT6 开始，旋腕后松手。板端识别不等待电脑确认。").pack(anchor="w", pady=4)
        self.plot = waveform(frame)
        self.worker = Thread(target=run, args=(args, self.stop, self.events), name="ST-Link recognition")
        self.worker.start()
        root.after(80, self.update)
        root.protocol("WM_DELETE_WINDOW", self.close)

    def update(self):
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == "status":
                    self.status.set(value)
                else:
                    self.show(*value)
        except Empty:
            pass
        finally:
            if not self.stop.is_set():
                self.root.after(80, self.update)

    def show(self, state, rows, reference):
        _, _, _, _, number, flags, count, duration, cycles, angle, a, b, c, label, _ = state
        self.window.set(f"窗口 #{number} · {duration} ms · {count} 点 · 计算 {cycles} CPU 周期")
        self.plot.set_rows(list(map(physical, rows)), duration)
        pc_flags, pc_result = reference

        def rejection(mask):
            return " / ".join(reason for bit, reason in protocol.FLAGS.items() if bit & mask)

        def display(name, angle_deg, scores):
            text = " / ".join(f"{category} {v:+.4f}" for category, v in zip(protocol.LABELS, scores))
            return f"{name} · Z {angle_deg:+.4f}° · {text}"

        self.mcu.set("MCU · " + (f"拒绝 · {rejection(flags)}" if flags else display(protocol.LABELS[label], angle, (a,b,c))))
        self.pc.set("PC 对照 · " + (f"拒绝 · {rejection(pc_flags)}" if pc_flags else display(*pc_result)))
        if flags or pc_flags:
            self.match.set("拒绝原因一致" if flags == pc_flags else "拒绝原因有差异")
        else:
            guess, ref_angle, ref_scores = pc_result
            score_error = max(abs(mcu - pc) for mcu, pc in zip((a, b, c), ref_scores))
            agreed = protocol.LABELS[label] == guess and abs(angle - ref_angle) < .001 and score_error < .001
            self.match.set(f"{'结果一致' if agreed else '结果有差异'} · 角度差 {angle-ref_angle:+.6f}°")

    def close(self):
        self.stop.set()
        if self.worker.is_alive():
            self.root.after(80, self.close)
        else:
            self.root.destroy()

"""Desktop BT6 data collector for Windows."""
import argparse
import os
from collections import deque
import csv
from datetime import datetime
import msvcrt
from pathlib import Path
from queue import Empty, SimpleQueue
from threading import Event, Thread
import tkinter as tk
from tkinter import messagebox, ttk
import winsound

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

from capture import FIELDS, LABELS
from capture_swd import DEFAULT_MAP
from collector import Collector, Phase, plan, review
from windows import ROOT

DEFAULT_PACK = Path(os.environ.get("STM32_DFP_PACK", "Keil.STM32F1xx_DFP.2.2.0.pack"))
MOVES = {
    ("CW", False): ("CW · 顺时针旋腕", "自然握持，沿约定观察方向顺时针旋腕一次，对应升温。"),
    ("CCW", False): ("CCW · 逆时针旋腕", "自然握持，沿同一观察方向逆时针旋腕一次，对应降温。"),
    ("NONE", True): ("NONE · 保持静止", "按下 BT6 后保持板子静止。"),
    ("NONE", False): ("NONE · 非目标动作", "按住期间拿起、放下或轻微晃动，避开目标旋腕。"),
}


class Waveform:
    def __init__(self, parent):
        self.figure = Figure(figsize=(4.7, 3.9), dpi=100, layout="tight")
        self.figure.patch.set_facecolor("#f4f6fa")
        self.axes = self.figure.subplots(2, 1, sharex=True)
        self.lines = []
        for axis, unit in zip(self.axes, ("Acceleration (g)", "Gyroscope (deg/s)")):
            self.lines.extend(axis.plot([], [], label=label, color=color, linewidth=1.25)[0]
                              for label, color in zip("XYZ", ("#b43955", "#007f74", "#315cc4")))
            axis.set_ylabel(unit)
            axis.grid(alpha=.25)
            axis.legend(loc="upper right", ncols=3, fontsize=8)
        self.axes[1].set_xlabel("Time (s)")
        self.canvas = FigureCanvasTkAgg(self.figure, master=parent)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)
        self.canvas.get_tk_widget().configure(highlightthickness=0)
        self.set_rows([])

    def set_rows(self, rows, duration_ms=None):
        origin = rows[0][0] if rows else 0
        times = [((row[0] - origin) & 0xFFFFFFFF) / 1000 for row in rows]
        for i, line in enumerate(self.lines, 1):
            line.set_data(times, [row[i] for row in rows])
        self.axes[-1].set_autoscalex_on(duration_ms is None)
        for axis in self.axes:
            axis.relim()
            axis.autoscale_view()
        if duration_ms:
            self.axes[-1].set_xlim(0, max(duration_ms / 1000, times[-1] if times else 0))
        self.canvas.draw_idle()


class App:
    def __init__(self, root, args):
        self.root = root
        root.title("BT6 手势数据采集")
        root.geometry("1180x820")
        root.minsize(920, 690)
        root.option_add("*Font", "{Microsoft YaHei UI} 10")
        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure("Title.TLabel", font=("Microsoft YaHei UI", 21, "bold"), foreground="#173354")
        style.configure("Cue.TLabel", font=("Microsoft YaHei UI", 16, "bold"), foreground="#154684")
        style.configure("Status.TLabel", foreground="#214c76")
        self.commands, self.events = SimpleQueue(), SimpleQueue()
        self.stop = Event()
        self.phase = None
        self.request_pending = False
        self.online = False
        self.last_result = tk.StringVar(value="")
        self.live_rows = deque(maxlen=500)
        self.items = []
        self.selected = None
        self.counts = {}
        self.history = tk.StringVar(value="")
        self.ready = tk.StringVar(value="未就绪")
        self.armed = False
        self.label = tk.StringVar(value="CW")
        self.goal = tk.StringVar(value="10")
        self.stationary = tk.BooleanVar(value=False)
        self.sound = tk.BooleanVar(value=True)
        self.link = tk.StringVar(value="ST-Link 连接中")
        self.status = tk.StringVar(value="正在打开采集后端")
        self.cue = tk.StringVar(value="选择或创建采集会话")
        self.hint = tk.StringVar(value="")
        self.progress = tk.StringVar(value="CW —   |   CCW —   |   NONE —")
        self.quality = tk.StringVar(value="等待第一个窗口")
        self.build()
        collector = Collector(ROOT, args.pack, args.map, self.commands, self.events,
                              self.stop, args.swd_hz)
        self.worker = Thread(target=collector.run, name="ST-Link")
        self.worker.start()
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(80, self.update_events)

    def build(self):
        page = ttk.Frame(self.root, padding=(16, 12))
        page.pack(fill="both", expand=True)
        ttk.Label(page, text="手势数据采集", style="Title.TLabel").pack(anchor="w")
        ttk.Label(page, textvariable=self.link).pack(anchor="w", pady=(2, 8))
        bar = ttk.Frame(page)
        bar.pack(fill="x")
        ttk.Button(bar, text="开始新采集", command=lambda: self.start("guided")).pack(side="left")
        self.resume = ttk.Button(bar, text="继续上次采集", command=lambda: self.start("continue"), state="disabled")
        self.resume.pack(side="left", padx=6)
        self.pause = ttk.Button(bar, text="暂停", command=self.toggle_pause, state="disabled")
        self.pause.pack(side="left")
        ttk.Checkbutton(bar, text="提示音", variable=self.sound).pack(side="right")
        manual = ttk.LabelFrame(page, text="手动采集", padding=(8, 5))
        manual.pack(fill="x", pady=(8, 4))
        ttk.Label(manual, text="动作").pack(side="left", padx=(0, 6))
        labels = ttk.Combobox(manual, textvariable=self.label, values=LABELS, width=7, state="readonly")
        labels.pack(side="left")
        labels.bind("<<ComboboxSelected>>", lambda _: self.update_stationary())
        ttk.Label(manual, text="目标").pack(side="left", padx=(10, 4))
        ttk.Spinbox(manual, textvariable=self.goal, from_=1, to=9999, width=6).pack(side="left")
        self.stationary_input = ttk.Checkbutton(manual, text="NONE 静置", variable=self.stationary, state="disabled")
        self.stationary_input.pack(side="left", padx=6)
        ttk.Button(manual, text="开始 / 更新手动采集", command=lambda: self.start("manual")).pack(side="left")
        self.archive = ttk.Button(manual, text="归档未归属窗口", command=self.archive_window)
        ttk.Separator(page).pack(fill="x", pady=6)
        ttk.Label(page, textvariable=self.cue, style="Cue.TLabel").pack(anchor="w")
        ttk.Label(page, textvariable=self.hint, wraplength=870).pack(anchor="w", pady=(2, 2))
        ttk.Label(page, textvariable=self.progress).pack(anchor="w")
        state_bar = ttk.Frame(page)
        state_bar.pack(fill="x", pady=(2, 5))
        ttk.Label(state_bar, textvariable=self.ready, style="Cue.TLabel").pack(side="left", padx=(0, 12))
        ttk.Label(state_bar, textvariable=self.status, wraplength=770, style="Status.TLabel").pack(side="left")
        plots = ttk.Frame(page)
        plots.pack(fill="both", expand=True)
        plots.columnconfigure(0, weight=1, uniform="plots")
        plots.columnconfigure(1, weight=1, uniform="plots")
        plots.rowconfigure(0, weight=1)
        live = ttk.LabelFrame(plots, text="实时六轴", padding=4)
        recent = ttk.LabelFrame(plots, text="所选完整窗口", padding=4)
        live.grid(row=0, column=0, sticky="nsew", padx=(0, 5))
        recent.grid(row=0, column=1, sticky="nsew", padx=(5, 0))
        self.live_plot = Waveform(live)
        self.recent_plot = Waveform(recent)
        result = ttk.Frame(page)
        result.pack(fill="x", pady=(8, 0))
        ttk.Label(result, text="会话").pack(side="left", padx=(0, 6))
        self.sessions = ttk.Combobox(result, textvariable=self.history, width=23, state="readonly",
                                     values=self.session_names())
        self.sessions.pack(side="left", padx=(0, 6))
        self.sessions.bind("<<ComboboxSelected>>", self.select_history)
        ttk.Label(result, text="样本").pack(side="left", padx=(0, 6))
        self.samples = ttk.Combobox(result, width=25, state="readonly")
        self.samples.pack(side="left", padx=(0, 6))
        self.samples.bind("<<ComboboxSelected>>", self.select_window)
        self.reject = ttk.Button(result, text="排除所选样本", state="disabled", command=self.reject_window)
        self.reject.pack(side="left")
        ttk.Label(result, textvariable=self.last_result).pack(side="right")
        ttk.Label(page, textvariable=self.quality, wraplength=870).pack(anchor="w", pady=(6, 0))

    def session_names(self):
        return sorted((p.name for p in ROOT.glob("*") if p.is_dir() and not p.name.startswith("_")), reverse=True)

    def update_stationary(self):
        active = self.label.get() == "NONE"
        self.stationary_input.configure(state="normal" if active else "disabled")
        if not active:
            self.stationary.set(False)

    def send(self, action, value):
        self.commands.put((action, value))

    def start(self, mode):
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:-3]
        if mode == "guided":
            self.send(mode, f"pilot-{stamp}")
        elif mode == "continue":
            self.send(mode, None)
        else:
            try:
                goal = int(self.goal.get())
                if not 1 <= goal <= 9999:
                    raise ValueError
            except ValueError:
                messagebox.showerror("目标数量", "请输入 1–9999 的整数。")
                return
            name = (self.phase.session if self.phase and not self.phase.guided and self.armed
                    else f"manual-{stamp}")
            self.send(mode, Phase(name, self.label.get(), self.stationary.get(), goal, False))
        self.request_pending = True
        self.ready.set("未就绪")
        self.update_cue()

    def toggle_pause(self):
        if self.phase and self.armed:
            self.send("pause", not self.phase.paused)
            self.request_pending = True
            self.ready.set("未就绪")
            self.update_cue()

    def archive_window(self):
        if messagebox.askyesno("归档窗口", "此窗口没有可确认的采集阶段。保留原始值并排除该窗口？"):
            self.send("archive", None)

    def reject_window(self):
        if self.selected and messagebox.askyesno("排除样本", "保留原始文件，将此窗口从有效样本中排除？"):
            self.send("reject", self.selected)

    def select_window(self, _=None):
        index = self.samples.current()
        if index < 0:
            return
        path, data, rejected = self.items[index]
        try:
            with (path / "raw.csv").open(encoding="utf-8", newline="") as csv_file:
                rows = [(int(row["t_ms"]), *(float(row[key]) for key in FIELDS[1:]))
                        for row in csv.DictReader(csv_file)]
        except (OSError, ValueError, KeyError) as error:
            self.quality.set(f"读取窗口失败：{error}")
            return
        self.show_window(path, data, rejected, rows)

    def show_window(self, path, data, rejected, rows):
        self.selected = path
        self.reject.configure(state="disabled" if rejected else "normal")
        q = data["quality"]
        result = "已排除" if rejected else ("通过" if q["passed"] else "质量异常")
        saturation = "、".join(f"{axis}×{n}" for axis, n in q["saturated"].items() if n) or "无"
        bias = q.get("stationary_gyro_mean_dps")
        extra = "   |   陀螺仪均值 " + ", ".join(f"{v:+.2f}" for v in bias) + " °/s" if bias else ""
        version = data.get("protocol_version", 3)
        duration = f"{data['duration_ms']} ms" if "duration_ms" in data else "旧版固定窗"
        cutoff = " · 达到上限" if data.get("truncated") else ""
        mismatch = " · 点数与窗长不符" if q.get("duration_consistent") is False else ""
        self.quality.set(f"{data['label']} · {result} · v{version} · {duration}{cutoff}{mismatch}   |   "
                         f"{q['samples']} 条   |   间隔 {q['interval_min_ms']}–{q['interval_max_ms']} ms，"
                         f"异常 {q['unexpected_intervals']}   |   饱和 {saturation}{extra}")
        self.recent_plot.set_rows(rows, data.get("duration_ms"))

    def select_history(self, _=None):
        selected = self.history.get()
        self.show_review(review(ROOT / selected) if selected else [])

    def show_review(self, items):
        self.items = list(reversed(items))
        self.samples.configure(values=[
            f"{data['saved_utc'][11:19]} UTC  {data['label']}  #{data['window_number']}"
            + ("  已排除" if rejected else ("  异常" if not data["quality"]["passed"] else ""))
            for _, data, rejected in self.items
        ])
        if self.items:
            index = next((i for i, item in enumerate(self.items) if item[0] == self.selected), 0)
            self.samples.current(index)
            self.select_window()
        else:
            self.selected = None
            self.samples.set("")
            self.reject.configure(state="disabled")
            self.recent_plot.set_rows([])
            self.quality.set("此会话还没有窗口")
        self.update_cue()

    def update_cue(self):
        if not self.phase:
            return
        p = self.phase
        move, instruction = MOVES[p.label, p.stationary]
        target = "上次采集" if not self.armed else ("采集已暂停" if p.paused else move)
        self.cue.set(f"{target}  ·  {p.session}")
        if self.request_pending:
            self.hint.set("阶段切换待处理。等阶段更新后再按 BT6。")
        elif not self.online:
            self.hint.set("等待 ST-Link 重连，继续使用当前阶段。")
        elif not self.armed:
            self.hint.set("点击继续上次采集，或开始新采集。")
        elif p.paused:
            self.hint.set("恢复后再按 BT6。")
        else:
            self.hint.set(f"{instruction} 按下开始，完成动作后松开。有效时长 150 ms–1.5 s。")
        goals = {(step.label, step.stationary): step.goal for step in plan(p.session)} if p.guided else {
            (p.label, p.stationary): p.goal
        }
        def cell(label, still=False):
            key = (label, still)
            return f"{self.counts.get(key, 0)}/{goals.get(key, '—')}"
        self.progress.set(f"CW {cell('CW')}    |    CCW {cell('CCW')}    |    "
                          f"NONE 静置 {cell('NONE', True)} · 非目标 {cell('NONE')}")
        self.pause.configure(text="恢复" if p.paused else "暂停",
                             state="normal" if self.armed else "disabled")
        self.resume.configure(state="normal")

    def update_events(self):
        try:
            self.process_events()
        finally:
            if not self.stop.is_set():
                self.root.after(100, self.update_events)

    def process_events(self):
        redraw = False
        while True:
            try:
                name, value = self.events.get_nowait()
            except Empty:
                break
            if name == "live":
                self.live_rows.extend(value)
                redraw = True
            elif name == "clear_live":
                self.live_rows.clear()
                redraw = True
            elif name == "link":
                online, sensor, code = value
                if self.online != online:
                    if not online and self.sound.get():
                        winsound.MessageBeep(winsound.MB_ICONEXCLAMATION)
                    self.online = online
                    self.update_cue()
                rate = "100 Hz（固件设定）" if online and sensor != "未识别" and code == 0 else "—"
                self.link.set(f"ST-Link {'已连接' if online else '未连接'}   |   {sensor}   |   {rate}")
            elif name == "status":
                self.status.set(value)
            elif name == "ready":
                self.ready.set("可以按 BT6" if value else "未就绪")
            elif name == "phase":
                changed_session = not self.phase or self.phase.session != value.session
                if changed_session:
                    self.last_result.set("")
                    self.history.set(value.session)
                    self.select_history()
                self.phase = value
                self.update_cue()
            elif name == "request":
                self.request_pending = value
                if not value and self.phase:
                    self.armed = True
                self.update_cue()
            elif name == "review":
                items, self.counts = value
                if self.phase and self.history.get() == self.phase.session:
                    self.show_review(items)
                self.update_cue()
            elif name == "history_changed":
                if value.name == self.history.get():
                    self.select_history()
            elif name == "cancelled":
                self.last_result.set("误触取消")
            elif name == "saved":
                path, data, rejected, rows = value
                self.sessions.configure(values=self.session_names())
                if self.phase and data["session"] == self.phase.session:
                    if self.history.get() == data["session"]:
                        self.show_window(path, data, rejected, rows)
                        index = next((i for i, item in enumerate(self.items) if item[0] == path), -1)
                        if index >= 0:
                            self.samples.current(index)
                    result = "已保存并排除" if rejected else ("保存成功" if data["quality"]["passed"] else "质量异常，原始窗口已保存")
                    self.last_result.set(f"#{data['window_number']} {result}")
                if self.sound.get() and not rejected:
                    winsound.MessageBeep(winsound.MB_OK if data["quality"]["passed"] else winsound.MB_ICONEXCLAMATION)
            elif name == "orphan":
                if value:
                    self.archive.pack(side="left", padx=8)
                else:
                    self.archive.pack_forget()
            elif name == "fatal":
                if self.sound.get():
                    winsound.MessageBeep(winsound.MB_ICONEXCLAMATION)
                self.ready.set("未就绪")
                self.link.set("采集后端已停止")
                self.status.set(value)
        if redraw:
            self.live_plot.set_rows(self.live_rows)

    def close(self):
        self.stop.set()
        self.status.set("正在关闭 SWD 会话…")
        self.wait_close()

    def wait_close(self):
        if self.worker.is_alive():
            self.root.after(80, self.wait_close)
        else:
            self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, default=DEFAULT_PACK)
    parser.add_argument("--map", type=Path)
    parser.add_argument("--swd-hz", type=int, default=1_000_000)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--recognize", action="store_true", help="Show independent MCU recognition")
    mode.add_argument("--shadow", action="store_true", help="Monitor the formal remote in shadow mode")
    args = parser.parse_args()
    if args.map is None:
        args.map = (Path(__file__).resolve().parents[2] / "code/Listings/IR_Remote.map") if args.shadow else DEFAULT_MAP
    root = tk.Tk()
    ROOT.mkdir(parents=True, exist_ok=True)
    with (ROOT / ".gui.lock").open("a+b") as lock:
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            root.withdraw()
            messagebox.showwarning("采集程序已运行", "请使用已打开的采集窗口。")
            root.destroy()
            return
        if args.shadow:
            from shadow_gui import ShadowApp
            ShadowApp(root, args)
        elif args.recognize:
            from recognition_gui import RecognitionApp
            RecognitionApp(root, args, Waveform)
        else:
            App(root, args)
        root.mainloop()


if __name__ == "__main__":
    main()

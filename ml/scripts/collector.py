"""Run one SWD connection and bind each saved window to the displayed phase."""
from dataclasses import asdict, dataclass, replace
import json
from queue import Empty
from struct import error as StructError
from types import SimpleNamespace

import capture_swd as swd
import recognition_swd as recognition
from windows import PROTOCOL_VERSION, WindowStore, durable, physical


@dataclass(frozen=True)
class Phase:
    session: str
    label: str
    stationary: bool
    goal: int
    guided: bool
    paused: bool = False


def plan(session):
    return (
        Phase(session, "CW", False, 10, True),
        Phase(session, "CCW", False, 10, True),
        Phase(session, "NONE", True, 5, True),
        Phase(session, "NONE", False, 5, True),
    )


def review(folder):
    items = []
    for label in ("CW", "CCW", "NONE"):
        for path in (folder / label).glob("w-*/metadata.json"):
            data = json.loads(path.read_text(encoding="utf-8"))
            items.append((path.parent, data, (path.parent / "rejected").exists()))
    return sorted(items, key=lambda item: item[1]["saved_utc"])


def totals(items):
    counts = {key: 0 for key in (("CW", False), ("CCW", False), ("NONE", True), ("NONE", False))}
    for _, data, rejected in items:
        if data.get("protocol_version") == PROTOCOL_VERSION and data["quality"]["passed"] and not rejected:
            counts[data["label"], bool(data["stationary"])] += 1
    return counts


def first_incomplete(session, counts):
    return next((p for p in plan(session) if counts[p.label, p.stationary] < p.goal), None)


class Collector:
    def __init__(self, root, pack, map_path, commands, events, stop, frequency=1_000_000):
        self.root, self.pack, self.map_path = root, pack, map_path
        self.commands, self.events, self.stop = commands, events, stop
        self.frequency = frequency
        self.binding = root / ".gui-phase.json"
        self.phase = None
        self.request = None
        self.orphan = False
        self.last_status = None
        self.latest = None
        self.last_cancelled = None
        self.items = []
        self.counts = totals([])
        self.next_sample = None
        self.guided_done = False
        self.enabled = False
        self.last_link = None
        self.last_ready = None

    def emit(self, name, value):
        self.events.put((name, value))

    def ready(self, ready):
        if ready != self.last_ready:
            self.last_ready = ready
            self.emit("ready", ready)

    def link(self, connected, sensor, status):
        state = (connected, sensor, status)
        if state != self.last_link:
            self.last_link = state
            self.emit("link", state)

    def status(self, message):
        if message != self.last_status:
            self.last_status = message
            self.emit("status", message)

    def refresh(self):
        self.items = review(self.root / self.phase.session) if self.phase else []
        self.counts = totals(self.items)
        self.emit("review", (self.items, self.counts))

    def persist_phase(self, phase):
        if phase is None:
            self.binding.unlink(missing_ok=True)
            return
        temporary = self.binding.with_suffix(".tmp")
        with temporary.open("w", encoding="utf-8") as out:
            json.dump(asdict(phase), out)
            durable(out)
        temporary.replace(self.binding)

    def publish_phase(self, phase, idle):
        if not idle():
            return False
        old = self.phase
        self.persist_phase(phase)
        try:
            clear = idle()
        except RuntimeError:
            self.persist_phase(old)
            raise
        if not clear:
            self.persist_phase(old)
            return False
        self.phase = phase
        self.refresh()
        self.emit("phase", phase)
        return True

    def apply_request(self, idle):
        action, value = self.request
        if action == "pause":
            phase = replace(self.phase, paused=value) if self.phase else None
        elif action == "continue":
            phase = replace(self.phase, paused=False) if self.phase else None
        elif action == "manual":
            phase = value
        else:
            counts = totals(review(self.root / value))
            phase = first_incomplete(value, counts) or plan(value)[-1]
        if phase and not self.publish_phase(phase, idle):
            return
        self.enabled = phase is not None
        self.request = None
        self.emit("request", False)

    def read_commands(self):
        while True:
            try:
                action, value = self.commands.get_nowait()
            except Empty:
                return
            if action == "reject":
                if (value.parent.parent.parent == self.root and
                        (value / "metadata.json").is_file()):
                    (value / "rejected").touch(exist_ok=True)
                    self.refresh()
                    self.emit("history_changed", value.parent.parent)
                    self.status("已排除样本，原始文件保留")
            elif action == "archive":
                if self.phase is None:
                    self.orphan = True
            else:
                self.ready(False)
                self.request = (action, value)
                self.emit("request", True)

    def handle_window(self, target, pending, window):
        number, _, rows, _, _ = window
        if self.phase is None and not self.orphan:
            self.status("有未归属的板上窗口。先归档，再为下一次按键选择阶段。")
            self.emit("orphan", True)
            return
        excluded = self.phase is None or self.phase.paused or self.guided_done or (
            not self.phase.guided and self.counts[self.phase.label, self.phase.stationary] >= self.phase.goal
        )
        phase = self.phase or Phase("_unassigned", "UNASSIGNED", False, 0, False)
        self.status("窗口完成，正在保存")
        args = SimpleNamespace(session=phase.session, label=phase.label,
                               stationary=phase.stationary, windows=None)
        dest = WindowStore(args, "swd", None).save(*window, excluded=excluded)
        metadata = json.loads((dest / "metadata.json").read_text(encoding="utf-8"))
        rejected = (dest / "rejected").exists()
        if self.latest != dest:
            self.refresh()
            self.latest = dest
            self.emit("saved", (dest, metadata, rejected, list(map(physical, rows))))
        swd.acknowledge(target, pending, number)
        self.orphan = False
        self.emit("orphan", False)
        if rejected:
            self.status("原始窗口已保存并排除，已提交确认")
        elif metadata["quality"]["passed"]:
            self.status("保存成功，已提交确认。请松开 BT6。")
        else:
            self.status("质量异常，原始窗口已保存。请松开后补采。")

    def live_rows(self, target, address, head):
        first = self.next_sample
        if first is None or first > head:
            self.emit("clear_live", None)
            first = max(0, head - 10)
        if head - first >= swd.CAPACITY:
            first = max(0, head - 32)
        count = min(head - first, swd.CAPACITY - first % swd.CAPACITY, 32)
        if count:
            rows = list(swd.read_rows(target, address, first, count))
            newer = swd.read_header(target, address)[1]
            if first + count <= newer < first + swd.CAPACITY:
                self.next_sample = first + count
                self.emit("live", list(map(physical, rows)))

    def poll(self, target, address, pending):
        _, head, who, sensor_status, state, _, _, cancelled = swd.read_header(target, address)
        header = swd.pending_header(target, pending)
        self.link(True, swd.SENSORS.get(who, "未识别"), sensor_status)
        window = swd.read_pending(target, pending, header)
        first_cancelled = self.last_cancelled
        self.last_cancelled = cancelled
        aborted = first_cancelled is not None and cancelled > first_cancelled
        if aborted:
            self.emit("cancelled", None)
        if window or header[3] or state != 0:
            self.ready(False)
        if window:
            self.handle_window(target, pending, window)
        elif header[3]:
            self.status("等待板端清除已确认窗口" if header[0] == header[4] else "窗口就绪，等待读取")
        elif state == 1:
            self.status("BT6 正在采集")
        elif state == 2:
            self.status("等 BT6 松开")
        elif state == 0:
            # Phase changes require a clean idle observation. Recheck after disk/USB work.
            def idle():
                return (not swd.pending_header(target, pending)[3]
                        and swd.read_header(target, address)[4] == 0)

            if self.request:
                self.apply_request(idle)
            if self.phase and self.phase.guided:
                next_phase = first_incomplete(self.phase.session, self.counts)
                self.guided_done = next_phase is None
                if self.enabled and next_phase and not self.phase.paused and next_phase != self.phase:
                    self.publish_phase(next_phase, idle)
            else:
                self.guided_done = False
            available = bool(self.enabled and self.phase and not sensor_status and
                             not self.request and not self.phase.paused and not self.guided_done and
                             self.counts[self.phase.label, self.phase.stationary] < self.phase.goal)
            self.ready(available and idle())
            if sensor_status:
                self.status(f"传感器异常：{swd.ERRORS.get(sensor_status, sensor_status)}")
            elif self.request:
                self.status("阶段切换待处理")
            elif not self.enabled:
                self.status("请选择开始新采集、继续上次采集或手动模式")
            elif self.guided_done:
                self.status("引导目标已完成")
            elif self.phase.paused:
                self.status("已暂停。此时按键产生的窗口会保存并排除。")
            elif self.counts[self.phase.label, self.phase.stationary] >= self.phase.goal:
                self.status("手动目标已达到")
            elif aborted:
                self.status("误触已取消。可再次按 BT6。")
            elif self.last_status != "误触已取消。可再次按 BT6。":
                self.status("等待 BT6 触发")
        self.live_rows(target, address, head)

    def run(self):
        try:
            self.root.mkdir(parents=True, exist_ok=True)
            if self.binding.exists():
                self.phase = Phase(**json.loads(self.binding.read_text(encoding="utf-8")))
                self.refresh()
                self.guided_done = self.phase.guided and first_incomplete(self.phase.session, self.counts) is None
                self.emit("phase", self.phase)
            address = swd.capture_address(self.map_path, "g_swd_capture")
            pending = swd.capture_address(self.map_path, "g_pending_window")
            mode_address = recognition.address_for(self.map_path, optional=True)
            if not self.pack.is_file():
                raise FileNotFoundError(f"找不到 STM32F1 设备包：{self.pack}")
            while not self.stop.is_set():
                self.read_commands()
                try:
                    session = swd.connect_probe(self.pack, frequency=self.frequency)
                    if session is None:
                        raise ConnectionError("未找到 ST-Link")
                    with session:
                        self.next_sample = None
                        while not self.stop.is_set():
                            self.read_commands()
                            try:
                                if mode_address is None or recognition.request_mode(
                                    session.target, mode_address,
                                    recognition.header(session.target, mode_address), recognition.CAPTURE
                                ):
                                    self.poll(session.target, address, pending)
                                else:
                                    self.ready(False)
                                    self.status("等待切换到采集模式，请松开 BT6")
                            except OSError as error:
                                self.status(f"文件操作失败，窗口保留等待重试：{error}")
                                self.stop.wait(1)
                            self.stop.wait(.045)
                except (RuntimeError, ConnectionError, StructError) as error:
                    self.ready(False)
                    self.link(False, "—", None)
                    self.status(f"等待 ST-Link 重连：{error}")
                    self.stop.wait(2)
        except (OSError, RuntimeError, ValueError, TypeError) as error:
            self.emit("fatal", str(error))

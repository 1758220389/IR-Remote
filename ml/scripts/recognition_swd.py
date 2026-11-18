"""SWD snapshot protocol for the independent recognizer."""
from struct import Struct

import capture_swd as swd
from windows import WINDOW_SAMPLES

SNAPSHOT = Struct("<9I4f2I")
VERSION = 0x00010000
CAPTURE = 1
RECOGNIZE = 2
LABELS = ("CCW", "CW", "NONE", "REJECT")
FLAGS = {1: "过短", 2: "达到截断上限", 4: "点数或时长异常", 8: "间隔异常", 16: "量程饱和"}


def address_for(map_path, optional=False):
    try:
        return swd.capture_address(map_path, "g_recognition")
    except RuntimeError:
        if optional:
            return None
        raise


def header(target, address):
    result = SNAPSHOT.unpack(bytes(target.read_memory_block8(address, SNAPSHOT.size)))
    if result[0] != VERSION:
        raise RuntimeError("请使用识别版固件和同一次构建生成的 map")
    return result


def request_mode(target, address, state, mode):
    if state[1] == mode:
        # Cancel a leftover switch request. In capture mode a pending window
        # can prevent firmware from clearing a request until the PC drains it.
        if state[2]:
            target.write32(address + 8, 0)
            target.flush()
        return True
    if state[2] != mode:
        target.write32(address + 8, mode)
        target.flush()
    return False


def read_snapshot(target, address, pending, state):
    if state[3] & 1 or not state[4]:
        return None
    count = state[6]
    if count > WINDOW_SAMPLES:
        raise RuntimeError("识别结果点数超出范围")
    raw = bytes(target.read_memory_block8(pending + swd.PENDING.size, count * swd.SAMPLE.size)) if count else b""
    if header(target, address) != state:
        return None
    return list(swd.SAMPLE.iter_unpack(raw))

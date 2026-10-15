# -*- coding: utf-8 -*-
"""
ota_send.py —— OTA 升级上位机（PC 侧）：把 bin 固件按项目现有协议发给板子。

两种模式（同一份板子代码都能用）：
  1) TCP server 模式（正常用法，走 WiFi）：
        python ota_send.py --tcp-server 8899 --bin app.bin --ver 2
     板子上的 ESP8266-01S 上链后会主动连过来（透传），本脚本把固件推进这条连接。
  2) 串口直连模式（台架调试，不用 WiFi）：
        python ota_send.py --serial COM7 --bin app.bin
     用 USB-TTL 直接接板子 PB11/PB10（USART3：TX 接 USB-TTL 的 RX，RX 接 USB-TTL 的 TX，必须共地）。
     注意串口模式下要**拔掉或断开** ESP8266，别让两个发送源打架。

帧协议：与工程 sys_frame 完全一致（AA 55 | CMD | LEN | DATA | CRC16低 | CRC16高 | 55 AA），
CRC16-MODBUS、范围 = CMD+LEN+DATA。命令字见 OTA/OTA1/ota/ota_part.h。

改协议时记得三处一起改：ota_part.h（命令字/状态码）、板子侧 ota_app.c、本脚本。
"""

import argparse
import binascii
import os
import socket
import struct
import sys
import threading
import time

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

# ---------------- 协议常量（与 ota_part.h 保持一致） ----------------
HEAD = b"\xAA\x55"
TAIL = b"\x55\xAA"

CMD_BEGIN = 0x10
CMD_DATA = 0x11
CMD_END = 0x12
CMD_STAT = 0x13
CMD_QUERY = 0x14

ST_OK = 0
ST_BUSY = 1
ST_ERASING = 2
ST_BADCMD = 3
ST_CRCERR = 4
ST_DONE = 5
ST_TOOBIG = 6
ST_IDLE = 7

ST_NAME = {
    ST_OK: "OK", ST_BUSY: "BUSY", ST_ERASING: "ERASING（板子正在擦下载区，要等几秒）",
    ST_BADCMD: "BADCMD（命令/偏移非法）", ST_CRCERR: "CRCERR（整镜像校验不过）",
    ST_DONE: "DONE（已写好，板子马上重启）", ST_TOOBIG: "TOOBIG（镜像超过运行区 48KB）",
    ST_IDLE: "IDLE",
}

CHUNK = 56              # 每帧数据字节数：必须是 4 的倍数（板子要按字写 Flash），见 ota_part.h
MAX_PAYLOAD = 64        # sys_frame 的载荷上限


def crc16(data, seed=0xFFFF):
    """CRC16-MODBUS：初值 0xFFFF、多项式 0xA001、结果不异或。与工程 sys_frame / ota_crc16 同一套。"""
    crc = seed
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return crc & 0xFFFF


def build_frame(cmd, payload=b""):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("载荷超过 %d 字节" % MAX_PAYLOAD)
    body = bytes([cmd, len(payload)]) + payload
    crc = crc16(body)
    return HEAD + body + struct.pack("<H", crc) + TAIL


class FrameReader:
    """从字节流里把帧切出来（按 头/长度/CRC/尾 四项校验，与 SYS_FRAME_Verify 判据一致）。"""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data):
        self.buf.extend(data)
        out = []
        while True:
            i = self.buf.find(HEAD)
            if i < 0:
                # 没有帧头：只留最后 1 字节，免得把跨包的 0xAA 丢掉
                del self.buf[:-1]
                break
            if i > 0:
                del self.buf[:i]
            if len(self.buf) < 4:
                break
            ln = self.buf[3]
            total = 2 + 1 + 1 + ln + 2 + 2
            if len(self.buf) < total:
                break
            frame = bytes(self.buf[:total])
            if frame[-2:] == TAIL:
                body = frame[2:4 + ln]
                got = struct.unpack("<H", frame[4 + ln:6 + ln])[0]
                if crc16(body) == got:
                    out.append((frame[2], frame[4:4 + ln]))
                    del self.buf[:total]
                    continue
            del self.buf[:2]        # 这个头是假的（数据里恰好出现 AA55），跳过它重新找
        return out


class Link(object):
    """两种传输方式的统一外壳：send() 发字节、recv() 收字节（非阻塞，返回 b"" 表示暂时没有）。"""

    def send(self, data):
        raise NotImplementedError

    def recv(self, timeout=0.0):
        raise NotImplementedError

    def close(self):
        pass


class TcpServerLink(Link):
    def __init__(self, port, timeout=180):
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("0.0.0.0", port))
        self.srv.listen(1)
        self.srv.settimeout(timeout)
        print("[PC] 等待板子上链：请给板子通电，ESP8266 会连到本机 %d 端口" % port)
        self.conn, addr = self.srv.accept()
        self.conn.settimeout(0.05)
        print("[PC] 已连上：%s:%d" % (addr[0], addr[1]))

    def send(self, data):
        self.conn.sendall(data)

    def recv(self, timeout=0.0):
        deadline = time.time() + timeout
        while True:
            try:
                d = self.conn.recv(4096)
                if not d:
                    raise SystemExit("[PC] 连接被对端关闭")
                return d
            except socket.timeout:
                if time.time() >= deadline:
                    return b""

    def close(self):
        try:
            self.conn.close()
        except Exception:
            pass
        try:
            self.srv.close()
        except Exception:
            pass


class SerialLink(Link):
    def __init__(self, port, baud):
        try:
            import serial          # pyserial
        except ImportError:
            raise SystemExit("[PC] 串口模式需要 pyserial：pip install pyserial")
        self.sp = serial.Serial(port, baud, timeout=0.05)
        print("[PC] 串口已打开：%s @ %d" % (port, baud))

    def send(self, data):
        self.sp.write(data)

    def recv(self, timeout=0.0):
        deadline = time.time() + timeout
        buf = b""
        while time.time() < deadline:
            n = self.sp.in_waiting
            buf += self.sp.read(n if n else 1)
        return buf

    def close(self):
        try:
            self.sp.close()
        except Exception:
            pass


def wait_stat(reader, link, timeout_s, note=""):
    """等一条 STAT 帧；期间收到的其它帧忽略。返回 (状态码, 已收字节数) 或 None（超时）。"""
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        for cmd, pl in reader.feed(link.recv(0.05)):
            if cmd != CMD_STAT or len(pl) < 5:
                continue
            code = pl[0]
            got = struct.unpack(">I", pl[1:5])[0]
            print("  [板] %s%s（已收 %d 字节）"
                  % (ST_NAME.get(code, "未知 0x%02X" % code), note, got))
            return code, got
    return None


def send_image(link, bin_path, ver, erase_grace_s):
    data = open(bin_path, "rb").read()
    size = len(data)
    img_crc = crc16(data)
    print("[PC] 固件：%s  %d 字节  CRC16=0x%04X  版本=%d" % (bin_path, size, img_crc, ver))
    if size == 0:
        raise SystemExit("[PC] 固件是空的")

    reader = FrameReader()

    # ---- 1) BEGIN：先问一句，再声明大小 ----
    link.send(build_frame(CMD_QUERY, b""))
    wait_stat(reader, link, 3.0, "（查询）")

    link.send(build_frame(CMD_BEGIN, struct.pack(">H", ver) + struct.pack(">I", size)))
    r = wait_stat(reader, link, 10.0, "（BEGIN）")
    if r is None:
        raise SystemExit("[PC] BEGIN 没等到应答：检查接线/波特率/板子是否跑在 App 里")
    if r[0] == ST_TOOBIG:
        raise SystemExit("[PC] 板子拒绝：镜像超过运行区 48KB（本方案在 RAM 里攒镜像）")
    if r[0] != ST_OK:
        raise SystemExit("[PC] BEGIN 失败：%s" % ST_NAME.get(r[0]))

    # ---- 2) DATA：发一帧等一条 STAT，进度对齐 ----
    off = 0
    t0 = time.time()
    while off < size:
        chunk = data[off:off + CHUNK]
        link.send(build_frame(CMD_DATA, struct.pack(">I", off) + chunk))
        r = wait_stat(reader, link, 5.0)
        if r is None:
            raise SystemExit("[PC] 第 %d 字节处的 DATA 没等到应答（丢帧/串口异常）" % off)
        if r[0] != ST_OK:
            raise SystemExit("[PC] DATA 被拒（%s）；请重新跑一次（BEGIN 会复位）" % ST_NAME.get(r[0]))
        if r[1] != off + len(chunk):
            raise SystemExit("[PC] 板子报的已收字节数对不上：板=%d 我=%d" % (r[1], off + len(chunk)))
        off += len(chunk)
        if off % (CHUNK * 20) < CHUNK:      # 每 20 帧打一次进度，别刷屏
            pct = off * 100 // size
            print("  [进度] %d%%（%d/%d）" % (pct, off, size))
    print("  [进度] 100%%（%d/%d），用时 %.1f 秒" % (size, size, time.time() - t0))

    # ---- 3) END：板子会先回 ERASING（擦 64KB 扇区，秒级），再回 DONE ----
    print("[PC] 发送 END（整镜像 CRC=0x%04X）；接下来板子要擦下载区，等 %d 秒不要断电"
          % (img_crc, erase_grace_s))
    link.send(build_frame(CMD_END, struct.pack(">H", img_crc)))
    t0 = time.time()
    while time.time() - t0 < erase_grace_s + 20:
        for cmd, pl in reader.feed(link.recv(0.1)):
            if cmd != CMD_STAT or len(pl) < 5:
                continue
            code = pl[0]
            print("  [板] %s" % ST_NAME.get(code, "0x%02X" % code))
            if code == ST_ERASING:
                continue                 # 还早，继续等 DONE
            if code == ST_DONE:
                print("[PC] 升级成功：板子正在重启，Bootloader 会把新固件搬到运行区")
                return 0
            if code in (ST_CRCERR, ST_BADCMD):
                raise SystemExit("[PC] 板子拒绝这次固件（%s）" % ST_NAME.get(code))
    raise SystemExit("[PC] 超时没等到 DONE：板子可能没在擦/写，看板子串口日志")


def main():
    ap = argparse.ArgumentParser(description="OTA 升级上位机（板1）")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--tcp-server", type=int, metavar="PORT", help="监听端口，等 ESP8266 透传连入")
    g.add_argument("--serial", metavar="COMx", help="串口直连（台架调试，不经 WiFi）")
    ap.add_argument("--baud", type=int, default=115200, help="串口波特率（默认 115200）")
    ap.add_argument("--bin", required=True, help="要下发的固件 bin（Keil 用 fromelf --bin 生成）")
    ap.add_argument("--ver", type=int, default=1, help="版本号，写进固件头（只给人看）")
    ap.add_argument("--erase-grace", type=int, default=8, help="擦除宽容时间（秒，默认 8）")
    a = ap.parse_args()

    # 自检：CRC 算法必须与工程一致（协议工作台验过的标准向量）
    if crc16(b"123456789") != 0x4B37:
        raise SystemExit("[PC] CRC16 自检失败：算法与工程不一致，先别下发固件")
    print("[PC] CRC16 自检通过（123456789 -> 0x4B37）")

    if not os.path.isfile(a.bin):
        raise SystemExit("[PC] 找不到固件文件：%s" % a.bin)

    link = TcpServerLink(a.tcp_server) if a.tcp_server else SerialLink(a.serial, a.baud)
    try:
        return send_image(link, a.bin, a.ver, a.erase_grace)
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main())

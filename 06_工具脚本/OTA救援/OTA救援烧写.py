#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA 串口救援烧写工具（PC 侧）

用途
    板子 App 区跑不起来、又没有 J-Link 的时候，用 USB-TTL 接板子的 USART1
    （PA9/PA10，115200 8N1），把 .bin 固件灌进 Bootloader 的下载区，
    Boot 自己完成"复验 → 搬到 App 区 → 作废下载区头"。

协议（与 OTA/OTA1_Boot/boot_rescue.c 一一对应，小端）
    写头: 'S' + size(4) + crc16(2) + ver(2)      -> 'R' 成功 / 'F' 失败
    数据: 'D' + off(4) + len(2) + data(len) + crc16(2，覆盖 off/len/data)
                                                  -> 'K' 成功 / 'N' 重发 / 'F' 失败
    结束: 'E'                                      -> 'V' 通过并安装 / 'X' 不通过

用法
    python OTA救援烧写.py COM7 firmware.bin
    python OTA救援烧写.py COM7 firmware.bin --ver 3 --chunk 128
"""

import sys
import time
import struct
import argparse

# pyserial 放到用时再导入：这样没装 pyserial 也能跑 --help 和自检，
# 不会一进来就报错退出。
try:
    import serial  # pyserial
except ImportError:
    serial = None

APP_MAX = 0x0000C000          # 48KB，与 ota_part.h 的 OTA_APP_SIZE 一致


def crc16(data: bytes, seed: int = 0xFFFF) -> int:
    """CRC16-MODBUS：多项式 0xA001，初值 0xFFFF —— 与固件端同一条。"""
    crc = seed
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def wait_byte(ser, want: bytes, timeout: float = 3.0) -> str:
    """等一个期望的回执字节；期间把 Boot 打印的日志顺便转出来。"""
    end = time.time() + timeout
    line = b""
    while time.time() < end:
        ch = ser.read(1)
        if not ch:
            continue
        if ch in want:
            return ch.decode(errors="replace")
        if ch in (b"\r", b"\n"):
            if line.strip():
                print("   boot>", line.decode(errors="replace").strip())
            line = b""
        else:
            line += ch
            if len(line) > 120:
                line = b""
    return ""


def send_header(ser, size: int, crc: int, ver: int) -> bool:
    pkt = b"S" + struct.pack("<IHH", size, crc, ver)
    for attempt in range(3):
        ser.write(pkt)
        ser.flush()
        r = wait_byte(ser, b"RF")
        if r == "R":
            print(f"   头写入成功 (size={size} crc=0x{crc:04X} ver={ver})")
            return True
        print(f"   头写入失败，重试 {attempt + 1}/3")
    return False


def send_data(ser, offset: int, chunk: bytes) -> bool:
    body = struct.pack("<IH", offset, len(chunk)) + chunk
    pkt = b"D" + body + struct.pack("<H", crc16(body))
    for attempt in range(5):
        ser.write(pkt)
        ser.flush()
        r = wait_byte(ser, b"KNF", timeout=4.0)
        if r == "K":
            return True
        if r == "N":
            continue                      # CRC 错，原样重发
        if r == "F":
            return False
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description="OTA 串口救援烧写")
    ap.add_argument("port", help="串口号，如 COM7")
    ap.add_argument("bin", help="固件 .bin 文件")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--ver", type=int, default=1, help="版本号（只给人看）")
    ap.add_argument("--chunk", type=int, default=128, help="每包字节数（≤256）")
    args = ap.parse_args()

    if serial is None:
        print("缺少 pyserial。先运行这句再试：")
        print("    python -m pip install pyserial -i https://pypi.tuna.tsinghua.edu.cn/simple")
        return 2

    data = open(args.bin, "rb").read()
    if not data:
        print("固件文件是空的")
        return 2
    if len(data) > APP_MAX:
        print(f"固件 {len(data)} 字节，超过 App 区上限 {APP_MAX}（48KB）")
        return 2
    if len(data) % 4:
        data += b"\xFF" * (4 - len(data) % 4)   # Flash 按字编程，尾部补齐
        print(f"固件补齐到 {len(data)} 字节（4 字节对齐）")

    crc = crc16(data)
    print(f"固件 {args.bin}: {len(data)} 字节, CRC16=0x{crc:04X}")
    print(f"串口 {args.port} @ {args.baud} ...")

    with serial.Serial(args.port, args.baud, timeout=0.05) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()

        print("[1/3] 等板子进入救援态（断电重上电，或它自己跳不进去）...")
        print("      提示：Boot 会每几百毫秒打一个 '.'，看到点就说明在等你了")
        # 先发一个 S，如果板子还没进救援态就会失败，重试几次
        if not send_header(ser, len(data), crc, args.ver):
            print("失败：板子没有回应。检查 3.3V/TX-RX 交叉/是否真进了救援态")
            return 3

        print(f"[2/3] 传数据（每包 {args.chunk} 字节）...")
        off = 0
        t0 = time.time()
        while off < len(data):
            piece = data[off:off + args.chunk]
            if not send_data(ser, off, piece):
                print(f"失败：偏移 0x{off:04X} 写不进去")
                return 4
            off += len(piece)
            if off % (args.chunk * 32) == 0 or off == len(data):
                pct = off * 100.0 / len(data)
                print(f"   {off}/{len(data)}  {pct:5.1f}%", end="\r", flush=True)
        dt = time.time() - t0
        print(f"\n   传输完成，用时 {dt:.1f}s，{len(data) / dt / 1024:.1f} KB/s")

        print("[3/3] 让 Boot 复验整镜像 ...")
        ser.write(b"E")
        ser.flush()
        r = wait_byte(ser, b"VX", timeout=10.0)
        if r == "V":
            print("成功：CRC 通过，Boot 正在把固件搬进 App 区并跳转")
            print("      （看到 App 的串口输出就说明起来了）")
            return 0
        print("失败：整镜像 CRC 不对（传输过程中丢了包）")
        return 5


if __name__ == "__main__":
    sys.exit(main())

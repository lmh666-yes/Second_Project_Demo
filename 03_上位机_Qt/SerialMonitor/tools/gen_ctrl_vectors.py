# 独立算一遍控制帧的字节（给 Qt 的 --cmdselftest 当硬编码向量用）
# 只依赖协议文字描述：AA 55 CMD LEN DATA CRC_lo CRC_hi 55 AA，CRC16-MODBUS 覆盖 CMD..DATA

def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def build(cmd: int, payload: bytes) -> bytes:
    body = bytes([cmd, len(payload)]) + payload
    c = crc16_modbus(body)
    return b'\xAA\x55' + body + bytes([c & 0xFF, (c >> 8) & 0xFF]) + b'\x55\xAA'


def show(name: str, frame: bytes) -> None:
    hexs = ' '.join('%02X' % b for b in frame)
    print(f'{name}: len={len(frame)}  {hexs}')
    return hexs


print('--- 请求帧 ---')
show('QUERY      (0x12, 空载荷)', build(0x12, b''))
show('SET_FWD=1  (0x10, 01)', build(0x10, b'\x01'))
show('SET_FWD=0  (0x10, 00)', build(0x10, b'\x00'))
show('SET_CACHE=1(0x11, 01)', build(0x11, b'\x01'))
show('CLR_CACHE  (0x13, 空载荷)', build(0x13, b''))
show('REBOOT     (0x14, 空载荷)', build(0x14, b''))

print()
print('--- 应答帧 ---')
# QUERY 应答：st=0 fwd=1 cache=1 rsv=0 rxOk=0x0123 rxBad=0x0004 cnt=0x00C8 lost=0x0002 up=0x00001234
qa = bytes([0x00, 0x01, 0x01, 0x00,
            0x01, 0x23,
            0x00, 0x04,
            0x00, 0xC8,
            0x00, 0x02,
            0x00, 0x00, 0x12, 0x34])
show('QUERY ACK  (0x92, 16B)', build(0x92, qa))
print('   payload =', ' '.join('%02X' % b for b in qa))

# SET_FWD 应答：st=0, 新值=1  → 2 字节
show('SET_FWD ACK(0x90, 00 01)', build(0x90, bytes([0x00, 0x01])))
# SET_FWD 应答：参数错 → st=1, 1 字节
show('SET_FWD ACK(0x90, 01)', build(0x90, bytes([0x01])))
# CLR_CACHE 应答：st=0, 清后条数 0 → 3 字节
show('CLR ACK    (0x93, 00 0000)', build(0x93, bytes([0x00, 0x00, 0x00])))

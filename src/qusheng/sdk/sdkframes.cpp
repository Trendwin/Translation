#include "qushengsdk.h"

quint16 QushengSdk::crc16(const QByteArray &data)
{
    // 沿用已与趋盛样例核对的CRC16：初值FFFF、多项式1021，覆盖帧头至数据末尾。
    // 不能为了其他命令更改这里的算法或覆盖范围；具体业务仅改变Cmd与payload。
    quint16 crc = 0xffff;
    for (char byte : data) {
        crc ^= quint16(quint8(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}

QByteArray QushengSdk::makeFrame(quint8 destination, quint8 source, quint8 sequence,
                               quint8 command, const QByteArray &payload)
{
    // 协议长度字节只计payload，最多255。SDK不维护序号或串口；调用者提供请求快照的序号。
    if (payload.size() > 255) return QByteArray();
    QByteArray frame;
    frame.reserve(payload.size() + 10);
    frame.append(char(0xaa)); frame.append(char(0xaa));
    frame.append(char(destination)); frame.append(char(source));
    frame.append(char(payload.size())); frame.append(char(~quint8(payload.size())));
    frame.append(char(sequence)); frame.append(char(command)); frame.append(payload);
    const quint16 crc = crc16(frame);
    frame.append(char(crc >> 8)); frame.append(char(crc));
    return frame;
}

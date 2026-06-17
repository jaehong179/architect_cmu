// WavFileReader.cpp — WAV 헤더 파싱(순수 파일 I/O). 구 MainWindow::OpenFile 의 파싱부 분리.
#include "WavFileReader.h"
#include <QFile>
#include <QDataStream>

WavFileInfo WavFileReader::readHeader(const QString &fileName)
{
    WavFileInfo info;
    QFile file(fileName);
    if (!file.exists()) { info.error = QStringLiteral("file does not exist"); return info; }
    if (!file.open(QIODevice::ReadOnly)) { info.error = QStringLiteral("file could not be opened"); return info; }

    TWaveHeader &header = info.header;
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);   // WAV 는 리틀 엔디안

    file.read(header.riffId, 4);
    in >> header.fileSize;
    file.read(header.waveId, 4);
    file.read(header.fmtId, 4);
    in >> header.fmtSize;
    in >> header.audioFormat;
    in >> header.numChannels;
    in >> header.sampleRate;
    in >> header.byteRate;
    in >> header.blockAlign;
    in >> header.bitsPerSample;

    // fmt 청크가 16바이트보다 크면 잔여 바이트 건너뜀
    if (header.fmtSize > 16) file.seek(file.pos() + (header.fmtSize - 16));

    // "data" 청크 탐색(fmt 바로 뒤가 아닐 수 있음)
    char chunkId[4];
    header.dataSize = 0;
    while (!file.atEnd()) {
        file.read(chunkId, 4);
        uint32_t chunkSize;
        in >> chunkSize;
        if (qstrncmp(chunkId, "data", 4) == 0) { header.dataSize = chunkSize; break; }
        file.seek(file.pos() + chunkSize);
    }

    file.close();
    info.ok = true;
    return info;
}

bool WavFileReader::isSupportedFormat(const TWaveHeader &h)
{
    return qstrncmp(h.riffId, "RIFF", 4) == 0
        && h.numChannels   == 1
        && h.bitsPerSample == 32
        && h.audioFormat   == 3;   // 3 = IEEE Float
}

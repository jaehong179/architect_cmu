#ifndef WAVFILEREADER_H
#define WAVFILEREADER_H
// WAV 파일 헤더 파싱 — 순수 파일 I/O. 오디오 장치/레이트 변경·UI 같은 부작용 없음(SoC).
//  과거에는 MainWindow::OpenFile 안에 RIFF/fmt/data 청크 파싱이 장치 전환 로직과 뒤섞여 있었으나,
//  '파일을 읽어 헤더를 해석하는' 책임만 이리로 분리한다. 포맷 수용 여부(레이트 일치 등) 정책 판단은
//  호출측(UI)에 남긴다 — 이 클래스는 '무엇이 들어있는가'만 답한다.
#include "WaveHeader.h"
#include <QString>

struct WavFileInfo
{
    bool        ok = false;   // 파일 열기 + 헤더 구조 파싱 성공 여부
    TWaveHeader header{};      // ok=true 일 때 채워짐(data 청크 크기 포함)
    QString     error;         // ok=false 시 사람이 읽을 사유
};

class WavFileReader
{
public:
    // WAV 헤더를 끝(data 청크)까지 파싱. 부작용 없음. ok=false 면 error 에 사유.
    static WavFileInfo readHeader(const QString &fileName);

    // 시계 분석기가 받아들이는 PCM 포맷인지: RIFF · 단일 채널 · 32-bit · IEEE Float(audioFormat==3).
    //  (샘플레이트 일치 여부는 현재 장치 설정에 달려 있으므로 호출측에서 별도 판단.)
    static bool isSupportedFormat(const TWaveHeader &h);
};

#endif // WAVFILEREADER_H

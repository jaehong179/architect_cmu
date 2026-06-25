#ifndef WAVREADER_H
#define WAVREADER_H
// WavReader — Qt 의존 없는 최소 WAV 리더. 프로젝트가 실제로 쓰는 포맷만 지원:
//  mono, 32-bit IEEE float PCM(audioFormat=3). RIFF 청크를 순회해 fmt/data 를 찾으므로
//  중간에 LIST 등 부가 청크가 있어도 동작한다(WavFileReader.cpp 의 44바이트 고정 가정보다 견고).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class WavReader
{
public:
    // 실패 시 false, errorOut 에 사유.
    bool open(const std::string &path, std::string &errorOut);
    void close();

    uint32_t sampleRate() const { return mSampleRate; }
    uint64_t totalSamples() const { return mDataSize / sizeof(float); }

    // count 개의 float 샘플을 읽어 out 에 채운다. 실제로 읽은 개수를 반환(EOF 시 count 미만).
    size_t readSamples(float *out, size_t count);

    ~WavReader();

private:
    FILE    *mFile      = nullptr;
    uint32_t mSampleRate = 0;
    uint32_t mDataSize   = 0;   // bytes
    uint32_t mDataRemaining = 0; // bytes left to read
};
#endif // WAVREADER_H

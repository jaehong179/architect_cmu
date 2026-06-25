#include "WavReader.h"
#include <cstring>

namespace {
bool readExact(FILE *f, void *buf, size_t n)
{
    return std::fread(buf, 1, n, f) == n;
}
} // namespace

bool WavReader::open(const std::string &path, std::string &errorOut)
{
    close();
    mFile = std::fopen(path.c_str(), "rb");
    if (!mFile) { errorOut = "cannot open file: " + path; return false; }

    char riffId[4], waveId[4];
    uint32_t riffSize;
    if (!readExact(mFile, riffId, 4) || !readExact(mFile, &riffSize, 4) || !readExact(mFile, waveId, 4)) {
        errorOut = "truncated RIFF header"; close(); return false;
    }
    if (std::memcmp(riffId, "RIFF", 4) != 0 || std::memcmp(waveId, "WAVE", 4) != 0) {
        errorOut = "not a RIFF/WAVE file"; close(); return false;
    }

    bool haveFmt = false, haveData = false;
    uint16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;

    while (!haveData) {
        char chunkId[4]; uint32_t chunkSize;
        if (!readExact(mFile, chunkId, 4) || !readExact(mFile, &chunkSize, 4)) {
            errorOut = "truncated chunk header (no data chunk found)"; close(); return false;
        }

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            if (chunkSize < 16) { errorOut = "fmt chunk too small"; close(); return false; }
            uint16_t blockAlign; uint32_t byteRate;
            if (!readExact(mFile, &audioFormat, 2) || !readExact(mFile, &numChannels, 2) ||
                !readExact(mFile, &mSampleRate, 4) || !readExact(mFile, &byteRate, 4) ||
                !readExact(mFile, &blockAlign, 2) || !readExact(mFile, &bitsPerSample, 2)) {
                errorOut = "truncated fmt chunk"; close(); return false;
            }
            const uint32_t extra = chunkSize - 16;
            if (extra > 0) std::fseek(mFile, (long)extra, SEEK_CUR);
            haveFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            if (!haveFmt) { errorOut = "data chunk before fmt chunk"; close(); return false; }
            mDataSize = chunkSize;
            mDataRemaining = chunkSize;
            haveData = true; // data 청크를 찾으면 그대로 멈춤(샘플은 이 위치부터 순차 read)
        } else {
            std::fseek(mFile, (long)chunkSize + (long)(chunkSize & 1), SEEK_CUR); // 짝수 정렬 패딩
        }
    }

    if (numChannels != 1) { errorOut = "only mono WAV is supported"; close(); return false; }
    if (!(audioFormat == 3 && bitsPerSample == 32)) {
        errorOut = "only 32-bit IEEE float WAV is supported (audioFormat=3, bitsPerSample=32)";
        close(); return false;
    }
    return true;
}

size_t WavReader::readSamples(float *out, size_t count)
{
    if (!mFile) return 0;
    const size_t wantBytes = count * sizeof(float);
    const size_t availBytes = wantBytes < mDataRemaining ? wantBytes : mDataRemaining;
    const size_t gotBytes = std::fread(out, 1, availBytes, mFile);
    mDataRemaining -= (uint32_t)gotBytes;
    return gotBytes / sizeof(float);
}

void WavReader::close()
{
    if (mFile) { std::fclose(mFile); mFile = nullptr; }
    mSampleRate = 0; mDataSize = 0; mDataRemaining = 0;
}

WavReader::~WavReader() { close(); }

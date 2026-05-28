#ifndef WAVEHEADER_H
#define WAVEHEADER_H
#include <QtTypes>
typedef struct
{
 char riffId[4];         // "RIFF"
 uint32_t fileSize;      // Size of file - 8 bytes
 char waveId[4];         // "WAVE"
 char fmtId[4];          // "fmt "
 uint32_t fmtSize;       // Usually 16 for PCM
 uint16_t audioFormat;   // 1 for PCM, 3 for IEEE Float
 uint16_t numChannels;
 uint32_t sampleRate;
 uint32_t byteRate;
 uint16_t blockAlign;
 uint16_t bitsPerSample; // 32 for float
 char dataId[4];         // "data"
 uint32_t dataSize;
} TWaveHeader;

#endif // WAVEHEADER_H

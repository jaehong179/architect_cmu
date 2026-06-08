#include "MfccExtractor.h"
#include <cmath>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --------------------------------------------------------------------------
// Construction
// --------------------------------------------------------------------------

MfccExtractor::MfccExtractor(int sampleRate)
{
    MfccConfig cfg;
    cfg.sampleRate = sampleRate;
    reconfigure(cfg);
}

MfccExtractor::MfccExtractor(const MfccConfig& config)
{
    reconfigure(config);
}

MfccExtractor::~MfccExtractor() = default;

// --------------------------------------------------------------------------
// Configuration
// --------------------------------------------------------------------------

static int nextPowerOf2(int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

void MfccExtractor::reconfigure(const MfccConfig& config)
{
    mSampleRate    = config.sampleRate;
    mFrameLength   = static_cast<int>(config.frameLengthMs * mSampleRate / 1000.0f);
    mHopLength     = static_cast<int>(config.frameHopMs * mSampleRate / 1000.0f);
    mFftSize       = (config.fftSize > 0) ? config.fftSize : nextPowerOf2(mFrameLength);
    mNumMelFilters = config.numMelFilters;
    mNumCoeffs     = config.numCoefficients;
    mLowFreqHz     = config.lowFreqHz;
    mHighFreqHz    = (config.highFreqHz > 0.0f) ? config.highFreqHz : (mSampleRate / 2.0f);

    buildHammingWindow();
    buildMelFilterbank();
    buildDctMatrix();

    // Allocate scratch buffers
    mFftReal.resize(mFftSize);
    mFftImag.resize(mFftSize);
    mPowerSpectrum.resize(mFftSize / 2 + 1);
    mMelEnergies.resize(mNumMelFilters);
    mMfccOut.resize(mNumCoeffs);

    // Reset streaming state
    mBuffer.resize(mFrameLength, 0.0f);
    mBufferPos = 0;
}

void MfccExtractor::setCallback(MfccCallback cb)
{
    mCallback = std::move(cb);
}

void MfccExtractor::reset()
{
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mBufferPos = 0;
}

// --------------------------------------------------------------------------
// Streaming input
// --------------------------------------------------------------------------

void MfccExtractor::feedSamples(const float* samples, int count)
{
    int pos = 0;
    while (pos < count) {
        // Fill buffer up to frame length
        int spaceInBuffer = mFrameLength - mBufferPos;
        int toCopy = std::min(spaceInBuffer, count - pos);
        std::memcpy(mBuffer.data() + mBufferPos, samples + pos, toCopy * sizeof(float));
        mBufferPos += toCopy;
        pos += toCopy;

        // When we have a full frame, process it
        if (mBufferPos >= mFrameLength) {
            processFrame(mBuffer.data());

            // Shift buffer by hop length (keep overlap portion)
            int overlap = mFrameLength - mHopLength;
            if (overlap > 0) {
                std::memmove(mBuffer.data(), mBuffer.data() + mHopLength, overlap * sizeof(float));
            }
            mBufferPos = overlap;
        }
    }
}

// --------------------------------------------------------------------------
// Frame processing: Window → FFT → Power → Mel → Log → DCT → MFCC
// --------------------------------------------------------------------------

void MfccExtractor::processFrame(const float* frame)
{
    // 1) Apply Hamming window and zero-pad to FFT size
    for (int i = 0; i < mFrameLength; i++) {
        mFftReal[i] = frame[i] * mWindow[i];
    }
    for (int i = mFrameLength; i < mFftSize; i++) {
        mFftReal[i] = 0.0f;
    }
    std::fill(mFftImag.begin(), mFftImag.end(), 0.0f);

    // 2) FFT
    fft(mFftReal, mFftImag);

    // 3) Power spectrum: |X[k]|^2 / N
    float invN = 1.0f / mFftSize;
    int specLen = mFftSize / 2 + 1;
    for (int k = 0; k < specLen; k++) {
        mPowerSpectrum[k] = (mFftReal[k] * mFftReal[k] + mFftImag[k] * mFftImag[k]) * invN;
    }

    // 4) Apply Mel filterbank
    for (int m = 0; m < mNumMelFilters; m++) {
        float sum = 0.0f;
        const float* filter = mMelFilterbank[m].data();
        for (int k = 0; k < specLen; k++) {
            sum += mPowerSpectrum[k] * filter[k];
        }
        // 5) Log energy (floor to avoid log(0))
        mMelEnergies[m] = std::log(std::max(sum, 1e-10f));
    }

    // 6) DCT-II to get MFCC coefficients
    for (int i = 0; i < mNumCoeffs; i++) {
        float sum = 0.0f;
        const float* dctRow = mDctMatrix[i].data();
        for (int m = 0; m < mNumMelFilters; m++) {
            sum += dctRow[m] * mMelEnergies[m];
        }
        mMfccOut[i] = sum;
    }

    // Deliver result
    if (mCallback) {
        mCallback(mMfccOut);
    }
}

// --------------------------------------------------------------------------
// Mel scale conversions
// --------------------------------------------------------------------------

float MfccExtractor::hzToMel(float hz)
{
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float MfccExtractor::melToHz(float mel)
{
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

// --------------------------------------------------------------------------
// Build precomputed tables
// --------------------------------------------------------------------------

void MfccExtractor::buildHammingWindow()
{
    mWindow.resize(mFrameLength);
    for (int i = 0; i < mFrameLength; i++) {
        mWindow[i] = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (mFrameLength - 1));
    }
}

void MfccExtractor::buildMelFilterbank()
{
    int specLen = mFftSize / 2 + 1;
    float melLow  = hzToMel(mLowFreqHz);
    float melHigh = hzToMel(mHighFreqHz);

    // Equally spaced points in Mel scale
    std::vector<float> melPoints(mNumMelFilters + 2);
    for (int i = 0; i < mNumMelFilters + 2; i++) {
        melPoints[i] = melLow + i * (melHigh - melLow) / (mNumMelFilters + 1);
    }

    // Convert back to Hz, then to FFT bin indices
    std::vector<int> binIndices(mNumMelFilters + 2);
    for (int i = 0; i < mNumMelFilters + 2; i++) {
        float hz = melToHz(melPoints[i]);
        binIndices[i] = static_cast<int>(std::floor((mFftSize + 1) * hz / mSampleRate));
        binIndices[i] = std::min(binIndices[i], specLen - 1);
    }

    // Build triangular filters
    mMelFilterbank.resize(mNumMelFilters);
    for (int m = 0; m < mNumMelFilters; m++) {
        mMelFilterbank[m].assign(specLen, 0.0f);
        int left   = binIndices[m];
        int center = binIndices[m + 1];
        int right  = binIndices[m + 2];

        // Rising slope
        for (int k = left; k <= center; k++) {
            if (center != left) {
                mMelFilterbank[m][k] = static_cast<float>(k - left) / (center - left);
            }
        }
        // Falling slope
        for (int k = center; k <= right; k++) {
            if (right != center) {
                mMelFilterbank[m][k] = static_cast<float>(right - k) / (right - center);
            }
        }
    }
}

void MfccExtractor::buildDctMatrix()
{
    // DCT-II (orthogonal, without 1/sqrt(N) normalization for simplicity)
    mDctMatrix.resize(mNumCoeffs);
    for (int i = 0; i < mNumCoeffs; i++) {
        mDctMatrix[i].resize(mNumMelFilters);
        for (int m = 0; m < mNumMelFilters; m++) {
            mDctMatrix[i][m] = std::cos(M_PI * i * (m + 0.5) / mNumMelFilters);
        }
    }
}

// --------------------------------------------------------------------------
// Radix-2 decimation-in-time FFT (in-place)
// --------------------------------------------------------------------------

void MfccExtractor::fft(std::vector<float>& real, std::vector<float>& imag)
{
    int N = static_cast<int>(real.size());

    // Bit-reversal permutation
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    // Cooley-Tukey butterfly
    for (int len = 2; len <= N; len <<= 1) {
        float angle = -2.0f * M_PI / len;
        float wRe = std::cos(angle);
        float wIm = std::sin(angle);

        for (int i = 0; i < N; i += len) {
            float curRe = 1.0f, curIm = 0.0f;
            for (int j = 0; j < len / 2; j++) {
                int u = i + j;
                int v = i + j + len / 2;
                float tRe = curRe * real[v] - curIm * imag[v];
                float tIm = curRe * imag[v] + curIm * real[v];
                real[v] = real[u] - tRe;
                imag[v] = imag[u] - tIm;
                real[u] += tRe;
                imag[u] += tIm;
                float newCurRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = newCurRe;
            }
        }
    }
}

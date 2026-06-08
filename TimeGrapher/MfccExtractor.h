#ifndef MFCC_EXTRACTOR_H
#define MFCC_EXTRACTOR_H

#include <vector>
#include <functional>
#include <cstddef>

/*
 * MfccExtractor — Streaming MFCC feature extractor.
 *
 * Feed PCM float samples via feedSamples(). When enough samples accumulate
 * to form a frame (with hop), the extractor computes MFCCs and invokes the
 * user-supplied callback with the resulting feature vector.
 *
 * Typical usage:
 *   MfccExtractor mfcc(48000);
 *   mfcc.setCallback([](const std::vector<float>& coeffs) { ... });
 *   mfcc.feedSamples(buffer, count);
 */

struct MfccConfig {
    int   sampleRate      = 48000;
    float frameLengthMs   = 25.0f;   // frame duration in milliseconds
    float frameHopMs      = 10.0f;   // hop between frames in milliseconds
    int   fftSize         = 0;       // 0 = auto (next power of 2 >= frameLength)
    int   numMelFilters   = 26;      // number of Mel filterbank channels
    int   numCoefficients = 13;      // number of MFCC coefficients to output
    float lowFreqHz       = 0.0f;    // lower edge of Mel filterbank
    float highFreqHz      = 0.0f;    // upper edge (0 = Nyquist)
};

class MfccExtractor
{
public:
    using MfccCallback = std::function<void(const std::vector<float>& mfccCoeffs)>;

    explicit MfccExtractor(int sampleRate = 48000);
    explicit MfccExtractor(const MfccConfig& config);
    ~MfccExtractor();

    void reconfigure(const MfccConfig& config);
    void setCallback(MfccCallback cb);
    void feedSamples(const float* samples, int count);
    void reset();

    // Accessors
    int frameLength() const { return mFrameLength; }
    int hopLength()   const { return mHopLength; }
    int fftSize()     const { return mFftSize; }
    int numCoeffs()   const { return mNumCoeffs; }

private:
    void buildMelFilterbank();
    void buildDctMatrix();
    void buildHammingWindow();
    void processFrame(const float* frame);

    // FFT (radix-2 in-place)
    void fft(std::vector<float>& real, std::vector<float>& imag);

    static float hzToMel(float hz);
    static float melToHz(float mel);

    MfccCallback mCallback;

    // Configuration
    int   mSampleRate;
    int   mFrameLength;
    int   mHopLength;
    int   mFftSize;
    int   mNumMelFilters;
    int   mNumCoeffs;
    float mLowFreqHz;
    float mHighFreqHz;

    // Precomputed tables
    std::vector<float>              mWindow;        // Hamming window
    std::vector<std::vector<float>> mMelFilterbank; // [numMelFilters][fftSize/2+1]
    std::vector<std::vector<float>> mDctMatrix;     // [numCoeffs][numMelFilters]

    // Internal sample buffer for overlapping frames
    std::vector<float> mBuffer;
    int                mBufferPos = 0;

    // Scratch buffers (avoid per-frame allocation)
    std::vector<float> mFftReal;
    std::vector<float> mFftImag;
    std::vector<float> mPowerSpectrum;
    std::vector<float> mMelEnergies;
    std::vector<float> mMfccOut;
};

#endif // MFCC_EXTRACTOR_H

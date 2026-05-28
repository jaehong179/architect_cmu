#pragma once

#include <QFile>
#include <QDataStream>
#include <QString>
#include <QVector>

/**
 * WavStreamWriter
 *
 * Streams 32-bit IEEE float PCM data to a WAV file incrementally.
 *
 * Usage:
 *   WavStreamWriter writer;
 *   writer.open("output.wav", 44100, 2);
 *   writer.write(floatBuffer, sampleCount);  // call as many times as needed
 *   writer.close();                          // patches the header with final sizes
 */
class WavStreamWriter
{
public:
    WavStreamWriter();
    ~WavStreamWriter();

    /**
     * Opens the file and writes a placeholder WAV header.
     *
     * @param filePath    Path to the output .wav file
     * @param sampleRate  Sample rate in Hz (e.g. 44100, 48000)
     * @param channels    Number of audio channels (1 = mono, 2 = stereo)
     * @return true on success
     */
    bool open(const QString &filePath, int sampleRate, int channels);

    /**
     * Appends interleaved float samples to the file.
     * Samples must be in [-1.0, 1.0] range.
     *
     * @param samples   Pointer to interleaved float samples
     * @param count     Total number of samples (frames × channels)
     * @return true on success
     */
    bool write(const float *samples, qsizetype count);

    /** Convenience overload for QVector<float> */
    bool write(const QVector<float> &samples);

    /**
     * Finalises the file by patching the RIFF and data chunk sizes,
     * then closes the file handle.
     * @return true on success
     */
    bool close();

    bool isOpen() const;
    quint64 framesWritten() const;
    int sampleRate() const;
    int channels() const;

private:
    bool writeHeader();
    bool patchHeader();

    QFile       m_file;
    QDataStream m_stream;

    int       m_sampleRate  = 0;
    int       m_channels    = 0;
    quint64   m_bytesWritten = 0;   // audio bytes only (after header)

    static constexpr int k_headerSize = 44; // bytes in a standard WAV header
};

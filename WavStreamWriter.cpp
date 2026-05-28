#include "WavStreamWriter.h"
#include <QDebug>

// ── WAV format constants ──────────────────────────────────────────────────────
static constexpr quint16 k_fmtIEEEFloat = 3;   // WAVE_FORMAT_IEEE_FLOAT
static constexpr quint16 k_bitsPerSample = 32;  // 32-bit float

// ── Constructor / Destructor ──────────────────────────────────────────────────

WavStreamWriter::WavStreamWriter() = default;

WavStreamWriter::~WavStreamWriter()
{
    if (m_file.isOpen())
        close();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool WavStreamWriter::open(const QString &filePath, int sampleRate, int channels)
{
    if (m_file.isOpen()) {
        qWarning() << "WavStreamWriter: already open, call close() first";
        return false;
    }

    if (sampleRate <= 0 || channels <= 0) {
        qWarning() << "WavStreamWriter: invalid sampleRate or channels";
        return false;
    }

    m_sampleRate   = sampleRate;
    m_channels     = channels;
    m_bytesWritten = 0;

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "WavStreamWriter: cannot open file:" << m_file.errorString();
        return false;
    }

    // QDataStream in LittleEndian mode – matches the WAV specification.
    m_stream.setDevice(&m_file);
    m_stream.setByteOrder(QDataStream::LittleEndian);
    m_stream.setFloatingPointPrecision(QDataStream::SinglePrecision);

    return writeHeader();
}

bool WavStreamWriter::write(const float *samples, qsizetype count)
{
    if (!m_file.isOpen()) {
        qWarning() << "WavStreamWriter: not open";
        return false;
    }
    if (!samples || count <= 0)
        return true;  // nothing to do, not an error

    // Write each float in native IEEE-754 little-endian format.
    // QDataStream handles the byte-order conversion automatically.
    for (qsizetype i = 0; i < count; ++i) {
        m_stream << samples[i];
    }

    if (m_stream.status() != QDataStream::Ok) {
        qWarning() << "WavStreamWriter: write error";
        return false;
    }

    m_bytesWritten += static_cast<quint64>(count) * sizeof(float);

    return true;
}

bool WavStreamWriter::write(const QVector<float> &samples)
{
    return write(samples.constData(), samples.size());
}

bool WavStreamWriter::close()
{
    if (!m_file.isOpen())
        return true;

    if (!patchHeader()) {
        qWarning() << "WavStreamWriter: failed to patch header";
        m_file.close();
        return false;
    }

    m_file.close();
    return true;
}

bool WavStreamWriter::isOpen()        const { return m_file.isOpen(); }
int  WavStreamWriter::sampleRate()    const { return m_sampleRate; }
int  WavStreamWriter::channels()      const { return m_channels; }

quint64 WavStreamWriter::framesWritten() const
{
    if (m_channels == 0) return 0;
    return m_bytesWritten / (sizeof(float) * static_cast<quint64>(m_channels));
}

// ── Private helpers ───────────────────────────────────────────────────────────

/**
 * Writes a standard 44-byte WAV header with placeholder sizes.
 * The sizes are patched with real values in patchHeader().
 *
 * WAV layout (all values little-endian):
 *
 *  Offset  Size  Field
 *  ------  ----  -----
 *   0       4    "RIFF"
 *   4       4    chunkSize       = fileSize - 8  (patched on close)
 *   8       4    "WAVE"
 *  12       4    "fmt "
 *  16       4    fmtChunkSize    = 16 for PCM/float
 *  20       2    audioFormat     = 3 (IEEE float)
 *  22       2    numChannels
 *  24       4    sampleRate
 *  28       4    byteRate        = sampleRate * channels * bytesPerSample
 *  32       2    blockAlign      = channels * bytesPerSample
 *  34       2    bitsPerSample   = 32
 *  36       4    "data"
 *  40       4    dataChunkSize   (patched on close)
 *  44       …    PCM audio data
 */
bool WavStreamWriter::writeHeader()
{
    const quint16 blockAlign    = static_cast<quint16>(m_channels * sizeof(float));
    const quint32 byteRate      = static_cast<quint32>(m_sampleRate * blockAlign);
    const quint32 placeholder   = 0;

    // RIFF chunk
    m_stream.writeRawData("RIFF", 4);
    m_stream << placeholder;            // chunkSize – patched later
    m_stream.writeRawData("WAVE", 4);

    // fmt sub-chunk
    m_stream.writeRawData("fmt ", 4);
    m_stream << quint32(16);            // fmtChunkSize
    m_stream << k_fmtIEEEFloat;
    m_stream << static_cast<quint16>(m_channels);
    m_stream << static_cast<quint32>(m_sampleRate);
    m_stream << byteRate;
    m_stream << blockAlign;
    m_stream << k_bitsPerSample;

    // data sub-chunk
    m_stream.writeRawData("data", 4);
    m_stream << placeholder;            // dataChunkSize – patched later

    if (m_stream.status() != QDataStream::Ok) {
        qWarning() << "WavStreamWriter: error writing header";
        return false;
    }

    Q_ASSERT(m_file.pos() == k_headerSize);
    return true;
}

/**
 * Seeks back and overwrites the two size fields in the WAV header.
 *
 *  chunkSize  @ offset 4  = (k_headerSize - 8) + m_bytesWritten
 *  dataSize   @ offset 40 = m_bytesWritten
 */
bool WavStreamWriter::patchHeader()
{
    const quint32 dataSize  = static_cast<quint32>(m_bytesWritten);
    const quint32 chunkSize = static_cast<quint32>((k_headerSize - 8) + m_bytesWritten);

    // Patch chunkSize at offset 4
    if (!m_file.seek(4)) {
        qWarning() << "WavStreamWriter: seek to offset 4 failed";
        return false;
    }
    m_stream << chunkSize;

    // Patch dataChunkSize at offset 40
    if (!m_file.seek(40)) {
        qWarning() << "WavStreamWriter: seek to offset 40 failed";
        return false;
    }
    m_stream << dataSize;

    return m_stream.status() == QDataStream::Ok;
}

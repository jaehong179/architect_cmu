#pragma once

#include <QImage>
#include <QtGlobal>
#include <cstddef>
#include <vector>

/**
    @file SoundImageRendererV2_fully_documented.h

    @brief Real-time folded sound-image renderer for watch/timegrapher-style
           acoustic displays.

    High-level purpose
    ------------------
    SoundImageRenderer converts incoming single-channel floating-point PCM into
    a scrolling/folded image where each image column represents one beat period.
    Within a column, vertical position represents time inside that beat period,
    and pixel intensity represents normalized signal magnitude.

    The renderer was built for these requirements:

        1. BPH can be unknown at initialization.
        2. Samples arriving before BPH is known must still be counted.
        3. Rendering starts only after BPH is known.
        4. Event markers use absolute sample indices from the same sample clock.
        5. Marker pixels must map to the same screen location as the original
           sample with that index.
        6. Time direction must be configurable:
              - bottom to top
              - top to bottom
        7. Marker bleed must be removed correctly when the image wraps.

    Coordinate systems
    ------------------
    Qt/QImage physical coordinates are always:

        x=0,y=0                  top-left
        x increases              to the right
        y increases              downward

    The renderer uses a logical "bucket" coordinate inside each beat column:

        natural_bucket = 0       start of the beat column
        natural_bucket increases later in the beat column

    The final conversion from logical bucket to QImage y coordinate depends on
    Config::vertical_time_direction:

        TimeStartsAtBottomMovesUp:
            y = height - 1 - display_bucket

        TimeStartsAtTopMovesDown:
            y = display_bucket

    Both signal drawing and marker drawing use the same conversion path.

    Absolute sample clock
    ---------------------
    reset(origin) declares that the next sample passed into processSamples() has
    absolute sample index "origin".

    processSamples() always advances:

        processed_samples_since_reset_

    even before BPH is known. This is critical because event markers are passed
    as absolute sample indices. If early pre-BPH samples are not counted, marker
    placement becomes shifted.

    Late-BPH behavior
    -----------------
    If Config::bph <= 0 during initialize(), the renderer is in "BPH unknown"
    mode.

    While BPH is unknown:
        - processSamples() increments the sample counter only
        - no audio is stored
        - no image columns are rendered

    When setBph(valid_bph) is called:
        - render state is cleared
        - the sample counter is preserved
        - render_epoch_sample_index_ is set to nextInputAbsoluteSampleIndex()
        - the next incoming sample starts the first visible render column

    Column timing
    -------------
    After BPH is known, the exact column period is:

        samples_per_column_exact = sample_rate_hz * 3600 / bph

    Column boundary k is:

        render_epoch_sample_index_ + round(k * samples_per_column_exact)

    Boundaries are computed only when starting a new column. The hot per-sample
    path does not recompute global beat phase.

    Marker model
    ------------
    Markers are deferred, not delayed.

    Correct:
        marker_sample_index == original_signal_sample_index

    Incorrect:
        marker_sample_index = original_signal_sample_index + display_delay

    Each visible image column stores the absolute sample range it represents:

        start_sample <= sample_index < end_sample

    Marker placement looks up the visible column containing the marker's sample
    index, then maps that index through the same column range used by the signal.

    Therefore, for a rendered sample N:

        signal sample N pixel == marker sample N pixel

    Marker bleed / wrap behavior
    ----------------------------
    Markers are centered filled squares:
        1 -> 1x1
        3 -> 3x3
        9 -> 9x9

    Size 3 and 9 can bleed horizontally into neighboring columns. When a column
    wraps and is reused, the renderer rebuilds a neighborhood around that column
    so stale marker pixels are removed.

    To rebuild safely, the renderer stores the sound-bin intensities for every
    visible column in rendered_bins_.
*/
class SoundImageRenderer
{
public:
    /**
        @brief Vertical direction for time within a beat column.

        This affects only final logical-bucket to QImage-y conversion.
        Sound and markers both use this same setting.
    */
    enum VerticalTimeDirection
    {
        /**
            Logical bucket 0 appears at the bottom of the image.
            Later samples in the beat appear higher.
        */
        TimeStartsAtBottomMovesUp = 0,

        /**
            Logical bucket 0 appears at the top of the image.
            Later samples in the beat appear lower.
        */
        TimeStartsAtTopMovesDown = 1
    };

    /**
        @brief Runtime configuration for the renderer.

        Most fields are safe to set once before initialize(). Some values can be
        changed later through dedicated setters, which clear render state while
        preserving the sample counter where appropriate.
    */
    struct Config
    {
        /**
            PCM input sample rate in Hz.

            Must be known at initialization because it is used for:
                - DC tracker coefficient
                - samples-per-column computation once BPH is known
        */
        double sample_rate_hz = 48000.0;

        /**
            Beats per hour.

            May be <= 0 at initialization if BPH is not yet known.

            If <= 0:
                processSamples() counts samples but does not render.

            Later call:
                setBph(valid_bph)
        */
        double bph = 0.0;

        /** Foreground color used for the rendered sound intensity. */
        QRgb sound_color = qRgba(255, 0, 0, 255);

        /** Background color used when clearing the image/columns. */
        QRgb background_color = qRgba(255, 255, 255, 255);

        /**
            Controls whether time inside each column visually moves upward or
            downward.
        */
        VerticalTimeDirection vertical_time_direction = TimeStartsAtBottomMovesUp;

        /**
            Number of completed columns after BPH lock to process but not draw.

            Purpose:
                - let DC tracker settle
                - let peak envelope settle
                - avoid using startup transients for center anchoring
        */
        int warmup_columns = 2;

        /**
            Number of completed post-warmup columns used to compute automatic
            vertical centering.

            These columns are buffered, not immediately drawn. Once the dominant
            band is found, they are flushed to the image with the locked offset.
        */
        int anchor_columns = 12;

        /**
            Time constant for simple one-pole DC removal.

            Larger values make DC tracking slower.
            Smaller values make DC tracking faster.
        */
        double dc_time_constant_sec = 0.25;

        /**
            Decay multiplier for peak-envelope normalization.

            The renderer normalizes magnitude by a slowly decaying peak estimate
            so the display remains visible across changing signal levels.
        */
        float peak_decay = 0.99995f;

        /**
            Display gamma for brightness shaping only.

            Formula:
                displayed = pow(normalized_intensity, gamma)

            Does not affect timing, marker placement, BPH, or sync.
        */
        float gamma = 0.5f;

        /**
            If true, redraw the active in-progress column at the end of each
            processSamples() call after centering has locked.

            For highest speed, set false and draw only completed columns.
        */
        bool live_preview_current_column = true;
    };

    SoundImageRenderer();
    ~SoundImageRenderer() = default;

    /**
        @brief Initialize renderer storage and bind it to a caller-owned QImage.

        @param image_argb32 Pointer to QImage using QImage::Format_ARGB32.
                           The caller owns the image and must keep it alive.
        @param cfg Renderer configuration.

        @return true on success, false for invalid image/config.

        Notes:
            - BPH may be unknown here.
            - If cfg.bph <= 0, processSamples() will count only until setBph().
    */
    bool initialize(QImage *image_argb32, const Config &cfg);

    /**
        @brief Reset absolute sample clock and render state to origin 0.
    */
    void reset();

    /**
        @brief Reset absolute sample clock and render state to a caller-specified
               origin.

        The next sample passed to processSamples() is treated as:
            next_input_absolute_sample_index
    */
    void reset(quint64 next_input_absolute_sample_index);

    void restart() { reset(); }
    void restart(quint64 next_input_absolute_sample_index) { reset(next_input_absolute_sample_index); }

    /** Change sound color for future redraws. */
    void setSoundColor(QRgb color);

    /** Change background color for future clears/redraws. */
    void setBackgroundColor(QRgb color);

    /**
        @brief Set or update BPH.

        May be called after samples have already been processed.

        If BPH becomes valid or changes:
            - visible/render state is cleared
            - sample counter is preserved
            - rendering restarts at nextInputAbsoluteSampleIndex()
    */
    void setBph(double bph);

    /**
        @brief Change input sample rate.

        If BPH is already valid, render state is cleared while the sample counter
        is preserved.
    */
    void setSampleRate(double sample_rate_hz);

    /**
        @brief Change vertical time direction.

        This clears visible/render state while preserving the absolute sample
        counter, because old columns were drawn using the previous orientation.
    */
    void setVerticalTimeDirection(VerticalTimeDirection direction);

    /**
        @brief Process a block of PCM samples.

        If BPH is unknown:
            - only the sample counter advances

        If BPH is known:
            - samples are folded into sequential columns
            - current column peak bins are updated
            - completed columns are rendered or buffered for centering
    */
    void processSamples(const float *samples, std::size_t count);

    /**
        @brief Draw/store an A-event marker at an absolute sample index.

        marker_side_pixels is normalized to one of:
            1, 3, 9
    */
    void markAEventAbsoluteSampleIndex(quint64 absolute_sample_index, QRgb color, int marker_side_pixels);

    /**
        @brief Draw/store a C-event marker at an absolute sample index.

        marker_side_pixels is normalized to one of:
            1, 3, 9
    */
    void markCEventAbsoluteSampleIndex(quint64 absolute_sample_index, QRgb color, int marker_side_pixels);

    int imageWidth() const { return width_; }
    int imageHeight() const { return height_; }
    int currentColumn() const { return write_column_; }
    int lastCompletedColumn() const { return last_completed_column_; }

    quint64 streamOriginSampleIndex() const { return stream_origin_sample_index_; }
    quint64 processedSamplesSinceReset() const { return processed_samples_since_reset_; }
    quint64 nextInputAbsoluteSampleIndex() const { return stream_origin_sample_index_ + processed_samples_since_reset_; }

    qint64 currentBeatIndex() const { return active_column_index_; }
    bool bandCenterLocked() const { return center_locked_; }
    int currentVerticalOffsetRows() const { return internal_vertical_offset_rows_; }

    bool bphValid() const { return bph_valid_; }
    double currentBph() const { return bph_valid_ ? cfg_.bph : 0.0; }
    double samplesPerColumnExact() const { return samples_per_column_exact_; }

    VerticalTimeDirection verticalTimeDirection() const { return cfg_.vertical_time_direction; }

private:
    /**
        Persistent marker overlay entry.

        Markers are stored so they can be reapplied whenever sound columns are
        redrawn or rebuilt after wrap.
    */
    struct Marker
    {
        quint64 absolute_sample_index = 0;
        QRgb color = qRgba(0, 0, 0, 0);
        int side = 1;
    };

    /**
        Metadata for one visible image column.

        The important invariant is:
            start_sample <= rendered samples in this column < end_sample

        Marker lookup uses this range to find the column that actually owns a
        given absolute sample index.
    */
    struct RenderedColumn
    {
        bool valid = false;
        qint64 column_index = -1;
        quint64 start_sample = 0;
        quint64 end_sample = 0;
        int vertical_offset_rows = 0;
    };

    /**
        Buffered startup column used before automatic centering is locked.
    */
    struct AnchorColumn
    {
        bool valid = false;
        qint64 column_index = -1;
        quint64 start_sample = 0;
        quint64 end_sample = 0;
    };

private:
    /** Caller-owned output image. Must be QImage::Format_ARGB32. */
    QImage *image_ = nullptr;

    /** Current configuration. */
    Config cfg_{};

    /** Image dimensions cached from image_. */
    int width_ = 0;
    int height_ = 0;

    /** Rounded integer sample rate for coefficient calculations. */
    quint64 sample_rate_int_ = 48000;

    /** Rounded integer BPH; valid only when bph_valid_ is true. */
    quint64 bph_int_ = 0;

    /** True once cfg_.bph is valid and rendering may occur. */
    bool bph_valid_ = false;

    /** Exact floating-point samples per displayed beat/column. */
    double samples_per_column_exact_ = 0.0;

    /** Absolute sample index of the first sample after reset(origin). */
    quint64 stream_origin_sample_index_ = 0;

    /** Number of samples consumed since reset(origin). */
    quint64 processed_samples_since_reset_ = 0;

    /** Absolute sample index where post-BPH rendering starts. */
    quint64 render_epoch_sample_index_ = 0;

    /** Whether active_start_sample_/active_end_sample_ describe a live column. */
    bool have_active_column_ = false;

    /** Logical column index since render_epoch_sample_index_. */
    qint64 active_column_index_ = 0;

    /** Absolute sample range for the active column. */
    quint64 active_start_sample_ = 0;
    quint64 active_end_sample_ = 0;

    /** State for simple DC removal. */
    double dc_mean_ = 0.0;
    double dc_alpha_ = 0.0;

    /** Peak-envelope state for display normalization. */
    float peak_env_ = 1.0e-6f;

    /** Current column bins, indexed by natural bucket. */
    std::vector<float> current_column_;

    /** Number of post-BPH columns skipped for warmup. */
    int warmup_columns_consumed_ = 0;

    /** True after automatic center offset has been chosen. */
    bool center_locked_ = false;

    /** Vertical offset applied to natural buckets before bucketToY(). */
    int internal_vertical_offset_rows_ = 0;

    /** Number of anchor columns collected so far. */
    int anchor_used_ = 0;

    /** Sum of anchor columns used to find dominant band. */
    std::vector<float> anchor_sum_;

    /** Buffered anchor column bin data. */
    std::vector<float> anchor_columns_buffer_;

    /** Metadata for buffered anchor columns. */
    std::vector<AnchorColumn> anchor_columns_meta_;

    /** Next screen column to write. Wraps modulo width_. */
    int write_column_ = 0;

    /** Most recent completed screen column. */
    int last_completed_column_ = -1;

    /** Metadata for each visible screen column. */
    std::vector<RenderedColumn> rendered_columns_;

    /**
        Stored sound bins for each visible screen column.

        Layout:
            rendered_bins_[x * height_ + natural_bucket]

        Used for marker-bleed cleanup when wrapping.
    */
    std::vector<float> rendered_bins_;

    /** Active marker overlay entries. */
    std::vector<Marker> active_markers_;

private:
    void recomputeDerived();
    void resetStateOnly(quint64 next_input_absolute_sample_index);
    void clearRenderStateKeepingSampleCounter();

    quint64 columnBoundarySample(qint64 boundary_index) const;
    void startActiveColumn(qint64 column_index);

    void clearWholeImage(QRgb color);
    void clearColumn(int x, QRgb color);
    void clearCurrentBuckets();

    void finalizeCurrentColumnAndAdvance();

    void renderBinsToColumn(int x, const float *bins, const RenderedColumn &meta);
    void renderCurrentColumnToImage();

    void commitAnchorColumn(const float *column, const AnchorColumn &meta);
    void flushBufferedAnchorColumns();

    int sampleToBucketInRange(quint64 absolute_sample_index,
                              quint64 start_sample,
                              quint64 end_sample) const;

    int applyVerticalOffset(int natural_bucket, int vertical_offset_rows) const;

    int bucketToY(int bucket) const;

    static QRgb lerpColor(QRgb bg, QRgb fg, float t);
    static int argmaxSmoothed5(const std::vector<float> &v);
    static int normalizeMarkerSidePixels(int requested_side);

    static int markerRadiusColumnsFromSide(int normalized_side);
    static int maxSupportedMarkerRadiusColumns();

    int lookupRenderedColumnBySampleIndex(quint64 absolute_sample_index) const;
    bool mapRenderedSampleToPixel(quint64 absolute_sample_index,
                                  int *out_x,
                                  int *out_y) const;

    void drawCenteredMarkerBlock(int x, int y, QRgb color, int marker_side_pixels);
    void drawCenteredMarkerContributionToColumn(int target_column,
                                                int center_x,
                                                int center_y,
                                                QRgb color,
                                                int marker_side_pixels);

    void reapplyMarkersForColumn(int column);
    void addPersistentMarkerFromAbsoluteSample(quint64 absolute_sample_index, QRgb color, int marker_side_pixels);
    void pruneOldMarkers();

    const float *renderedBinsPtr(int column) const;
    float *renderedBinsPtr(int column);
    void clearRenderedColumnStorage(int column);
    void rebuildColumnNeighborhood(int center_column);
};

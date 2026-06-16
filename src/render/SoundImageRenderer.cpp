#include "SoundImageRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>

SoundImageRenderer::SoundImageRenderer()
{
}

/*
    initialize()
    ------------
    Allocates internal buffers sized to the caller-provided image.

    This function intentionally does NOT require BPH. If cfg.bph <= 0, the
    renderer starts in count-only mode. That is required because the application
    may need a few samples to estimate BPH before it can display folded columns.
*/
bool SoundImageRenderer::initialize(QImage *image_argb32, const Config &cfg)
{
    if (!image_argb32) {
        return false;
    }
    if (image_argb32->format() != QImage::Format_ARGB32) {
        return false;
    }
    if (image_argb32->width() <= 0 || image_argb32->height() <= 0) {
        return false;
    }
    if (cfg.sample_rate_hz <= 0.0 ||
        cfg.warmup_columns < 0 || cfg.anchor_columns <= 0) {
        return false;
    }

    image_ = image_argb32;
    cfg_ = cfg;

    width_ = image_->width();
    height_ = image_->height();

    current_column_.assign(static_cast<std::size_t>(height_), 0.0f);
    anchor_sum_.assign(static_cast<std::size_t>(height_), 0.0f);
    anchor_columns_buffer_.assign(static_cast<std::size_t>(cfg_.anchor_columns * height_), 0.0f);
    anchor_columns_meta_.assign(static_cast<std::size_t>(cfg_.anchor_columns), AnchorColumn{});
    rendered_columns_.assign(static_cast<std::size_t>(width_), RenderedColumn{});
    rendered_bins_.assign(static_cast<std::size_t>(width_ * height_), 0.0f);
    active_markers_.clear();

    recomputeDerived();
    resetStateOnly(0);
    clearWholeImage(cfg_.background_color);

    return true;
}

void SoundImageRenderer::reset()
{
    reset(0);
}

void SoundImageRenderer::reset(quint64 next_input_absolute_sample_index)
{
    if (!image_) {
        return;
    }

    resetStateOnly(next_input_absolute_sample_index);
    clearWholeImage(cfg_.background_color);
}

/*
    recomputeDerived()
    ------------------
    Recomputes values derived from configuration.

    Important:
        sample_rate_hz is always required.
        bph is optional.

    If BPH is invalid, bph_valid_ becomes false and processSamples() will only
    advance the sample counter.
*/
void SoundImageRenderer::recomputeDerived()
{
    sample_rate_int_ = static_cast<quint64>(std::llround(cfg_.sample_rate_hz));
    if (sample_rate_int_ == 0) {
        sample_rate_int_ = 1;
    }

    if (cfg_.bph > 0.0) {
        bph_int_ = static_cast<quint64>(std::llround(cfg_.bph));
        if (bph_int_ == 0) {
            bph_int_ = 1;
        }

        bph_valid_ = true;
        samples_per_column_exact_ = (cfg_.sample_rate_hz * 3600.0) / cfg_.bph;
        if (samples_per_column_exact_ < 1.0) {
            samples_per_column_exact_ = 1.0;
        }
    } else {
        bph_int_ = 0;
        bph_valid_ = false;
        samples_per_column_exact_ = 0.0;
    }

    const double tc = std::max(1.0e-6, cfg_.dc_time_constant_sec);
    dc_alpha_ = 1.0 / (tc * static_cast<double>(sample_rate_int_));
    if (dc_alpha_ > 1.0) {
        dc_alpha_ = 1.0;
    }
}

void SoundImageRenderer::resetStateOnly(quint64 next_input_absolute_sample_index)
{
    stream_origin_sample_index_ = next_input_absolute_sample_index;
    processed_samples_since_reset_ = 0;

    render_epoch_sample_index_ = next_input_absolute_sample_index;

    have_active_column_ = false;
    active_column_index_ = 0;

    active_start_sample_ = render_epoch_sample_index_;
    active_end_sample_ = render_epoch_sample_index_ + 1;

    dc_mean_ = 0.0;
    peak_env_ = 1.0e-6f;

    warmup_columns_consumed_ = 0;
    center_locked_ = false;
    internal_vertical_offset_rows_ = 0;
    anchor_used_ = 0;

    write_column_ = 0;
    last_completed_column_ = -1;

    std::fill(current_column_.begin(), current_column_.end(), 0.0f);
    std::fill(anchor_sum_.begin(), anchor_sum_.end(), 0.0f);
    std::fill(anchor_columns_buffer_.begin(), anchor_columns_buffer_.end(), 0.0f);
    std::fill(anchor_columns_meta_.begin(), anchor_columns_meta_.end(), AnchorColumn{});
    std::fill(rendered_columns_.begin(), rendered_columns_.end(), RenderedColumn{});
    std::fill(rendered_bins_.begin(), rendered_bins_.end(), 0.0f);
    active_markers_.clear();
}

/*
    clearRenderStateKeepingSampleCounter()
    --------------------------------------
    Clears all visible/rendering state but preserves the absolute sample clock.

    Used when:
        - BPH becomes known after pre-BPH samples have already been counted
        - BPH changes
        - sample rate changes
        - vertical display direction changes

    Preserving the sample counter is the key synchronization requirement.
*/
void SoundImageRenderer::clearRenderStateKeepingSampleCounter()
{
    render_epoch_sample_index_ = nextInputAbsoluteSampleIndex();

    have_active_column_ = false;
    active_column_index_ = 0;

    active_start_sample_ = render_epoch_sample_index_;
    active_end_sample_ = render_epoch_sample_index_ + 1;

    dc_mean_ = 0.0;
    peak_env_ = 1.0e-6f;

    warmup_columns_consumed_ = 0;
    center_locked_ = false;
    internal_vertical_offset_rows_ = 0;
    anchor_used_ = 0;

    write_column_ = 0;
    last_completed_column_ = -1;

    std::fill(current_column_.begin(), current_column_.end(), 0.0f);
    std::fill(anchor_sum_.begin(), anchor_sum_.end(), 0.0f);
    std::fill(anchor_columns_buffer_.begin(), anchor_columns_buffer_.end(), 0.0f);
    std::fill(anchor_columns_meta_.begin(), anchor_columns_meta_.end(), AnchorColumn{});
    std::fill(rendered_columns_.begin(), rendered_columns_.end(), RenderedColumn{});
    std::fill(rendered_bins_.begin(), rendered_bins_.end(), 0.0f);
    active_markers_.clear();
}

void SoundImageRenderer::setSoundColor(QRgb color)
{
    cfg_.sound_color = color;
}

void SoundImageRenderer::setBackgroundColor(QRgb color)
{
    cfg_.background_color = color;
}

void SoundImageRenderer::setBph(double bph)
{
    if (bph <= 0.0) {
        return;
    }

    const bool was_valid = bph_valid_;
    const double old_bph = cfg_.bph;

    cfg_.bph = bph;
    recomputeDerived();

    if (!was_valid || old_bph != bph) {
        clearRenderStateKeepingSampleCounter();
        if (image_) {
            clearWholeImage(cfg_.background_color);
        }
    }
}

void SoundImageRenderer::setSampleRate(double sample_rate_hz)
{
    if (sample_rate_hz <= 0.0) {
        return;
    }

    const double old_rate = cfg_.sample_rate_hz;
    cfg_.sample_rate_hz = sample_rate_hz;
    recomputeDerived();

    if (old_rate != sample_rate_hz && bph_valid_) {
        clearRenderStateKeepingSampleCounter();
        if (image_) {
            clearWholeImage(cfg_.background_color);
        }
    }
}

void SoundImageRenderer::setVerticalTimeDirection(VerticalTimeDirection direction)
{
    if (cfg_.vertical_time_direction == direction) {
        return;
    }

    cfg_.vertical_time_direction = direction;

    if (image_) {
        clearRenderStateKeepingSampleCounter();
        clearWholeImage(cfg_.background_color);
    }
}

/*
    columnBoundarySample()
    ----------------------
    Computes absolute sample index for a column boundary.

    Boundary 0 is render_epoch_sample_index_.

    Boundary k:
        render_epoch_sample_index_ + round(k * samples_per_column_exact_)

    This supports non-integer samples per column without doing expensive phase
    math for every input sample.
*/
quint64 SoundImageRenderer::columnBoundarySample(qint64 boundary_index) const
{
    if (boundary_index <= 0 || !bph_valid_) {
        return render_epoch_sample_index_;
    }

    const long double exact_offset =
        static_cast<long double>(boundary_index) *
        static_cast<long double>(samples_per_column_exact_);

    const quint64 offset = static_cast<quint64>(std::llround(exact_offset));
    return render_epoch_sample_index_ + offset;
}

void SoundImageRenderer::startActiveColumn(qint64 column_index)
{
    have_active_column_ = true;
    active_column_index_ = column_index;

    active_start_sample_ = columnBoundarySample(column_index);
    active_end_sample_ = columnBoundarySample(column_index + 1);

    if (active_end_sample_ <= active_start_sample_) {
        active_end_sample_ = active_start_sample_ + 1;
    }

    clearCurrentBuckets();
}

void SoundImageRenderer::clearWholeImage(QRgb color)
{
    if (!image_) {
        return;
    }

    for (int y = 0; y < height_; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image_->scanLine(y));
        for (int x = 0; x < width_; ++x) {
            row[x] = color;
        }
    }
}

void SoundImageRenderer::clearColumn(int x, QRgb color)
{
    if (!image_ || x < 0 || x >= width_) {
        return;
    }

    for (int y = 0; y < height_; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image_->scanLine(y));
        row[x] = color;
    }
}

void SoundImageRenderer::clearCurrentBuckets()
{
    std::fill(current_column_.begin(), current_column_.end(), 0.0f);
}

/*
    sampleToBucketInRange()
    -----------------------
    Converts an absolute sample index into a natural bucket inside one column.

    The range is:
        start_sample <= sample < end_sample

    The output bucket is:
        0 <= bucket < height_

    This is used by both:
        - signal sample placement
        - marker sample placement

    That shared path is what keeps markers aligned with signal pixels.
*/
int SoundImageRenderer::sampleToBucketInRange(quint64 absolute_sample_index,
                                              quint64 start_sample,
                                              quint64 end_sample) const
{
    if (height_ <= 0 || end_sample <= start_sample) {
        return 0;
    }

    if (absolute_sample_index < start_sample) {
        absolute_sample_index = start_sample;
    }
    if (absolute_sample_index >= end_sample) {
        absolute_sample_index = end_sample - 1;
    }

    const quint64 offset = absolute_sample_index - start_sample;
    const quint64 length = end_sample - start_sample;

#if defined(__SIZEOF_INT128__)
    __uint128_t bucket =
        (static_cast<__uint128_t>(offset) * static_cast<__uint128_t>(height_)) /
        static_cast<__uint128_t>(length);
    if (bucket >= static_cast<__uint128_t>(height_)) {
        bucket = static_cast<__uint128_t>(height_ - 1);
    }
    return static_cast<int>(bucket);
#else
    int bucket = static_cast<int>(
        (static_cast<long double>(offset) * static_cast<long double>(height_)) /
        static_cast<long double>(length));
    if (bucket < 0) {
        bucket = 0;
    }
    if (bucket >= height_) {
        bucket = height_ - 1;
    }
    return bucket;
#endif
}

int SoundImageRenderer::applyVerticalOffset(int natural_bucket, int vertical_offset_rows) const
{
    if (height_ <= 0) {
        return 0;
    }

    return ((natural_bucket + vertical_offset_rows) % height_ + height_) % height_;
}

/*
    bucketToY()
    -----------
    Converts logical display bucket to physical QImage y coordinate.

    Qt uses y=0 at the top. The renderer supports either visual time direction.
*/
int SoundImageRenderer::bucketToY(int bucket) const
{
    if (cfg_.vertical_time_direction == TimeStartsAtTopMovesDown) {
        return bucket;
    }

    return (height_ - 1) - bucket;
}

QRgb SoundImageRenderer::lerpColor(QRgb bg, QRgb fg, float t)
{
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }

    auto lerp1 = [t](int a, int b) -> int {
        return static_cast<int>(std::lround(a + (b - a) * t));
    };

    return qRgba(
        lerp1(qRed(bg),   qRed(fg)),
        lerp1(qGreen(bg), qGreen(fg)),
        lerp1(qBlue(bg),  qBlue(fg)),
        lerp1(qAlpha(bg), qAlpha(fg)));
}

int SoundImageRenderer::argmaxSmoothed5(const std::vector<float> &v)
{
    if (v.empty()) {
        return 0;
    }

    int best_idx = 0;
    float best_val = -std::numeric_limits<float>::infinity();

    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        const int lo = std::max(0, i - 2);
        const int hi = std::min(static_cast<int>(v.size()) - 1, i + 2);

        float sum = 0.0f;
        int n = 0;
        for (int j = lo; j <= hi; ++j) {
            sum += v[static_cast<std::size_t>(j)];
            ++n;
        }

        const float avg = (n > 0) ? (sum / static_cast<float>(n)) : 0.0f;
        if (avg > best_val) {
            best_val = avg;
            best_idx = i;
        }
    }

    return best_idx;
}

int SoundImageRenderer::normalizeMarkerSidePixels(int requested_side)
{
    if (requested_side <= 1) {
        return 1;
    }

    const int d1 = std::abs(requested_side - 1);
    const int d3 = std::abs(requested_side - 3);
    const int d9 = std::abs(requested_side - 9);

    if (d1 <= d3 && d1 <= d9) {
        return 1;
    }
    if (d3 <= d1 && d3 <= d9) {
        return 3;
    }
    return 9;
}

int SoundImageRenderer::markerRadiusColumnsFromSide(int normalized_side)
{
    return normalized_side / 2;
}

int SoundImageRenderer::maxSupportedMarkerRadiusColumns()
{
    return markerRadiusColumnsFromSide(9);
}

const float *SoundImageRenderer::renderedBinsPtr(int column) const
{
    if (column < 0 || column >= width_) {
        return nullptr;
    }
    return &rendered_bins_[static_cast<std::size_t>(column * height_)];
}

float *SoundImageRenderer::renderedBinsPtr(int column)
{
    if (column < 0 || column >= width_) {
        return nullptr;
    }
    return &rendered_bins_[static_cast<std::size_t>(column * height_)];
}

void SoundImageRenderer::clearRenderedColumnStorage(int column)
{
    if (column < 0 || column >= width_) {
        return;
    }

    rendered_columns_[static_cast<std::size_t>(column)] = RenderedColumn{};
    float *dst = renderedBinsPtr(column);
    if (dst) {
        std::fill(dst, dst + height_, 0.0f);
    }
}

/*
    renderBinsToColumn()
    --------------------
    Renders a complete column from bin data.

    Also stores a copy of the bins in rendered_bins_ so the column can be rebuilt
    later if marker bleed must be cleaned up after wrap.
*/
void SoundImageRenderer::renderBinsToColumn(int x,
                                            const float *bins,
                                            const RenderedColumn &meta)
{
    if (!image_ || !bins || x < 0 || x >= width_) {
        return;
    }

    float *stored = renderedBinsPtr(x);
    if (stored) {
        std::copy(bins, bins + height_, stored);
    }

    clearColumn(x, cfg_.background_color);

    for (int natural_bucket = 0; natural_bucket < height_; ++natural_bucket) {
        float v = bins[natural_bucket];
        if (v < 0.0f) {
            v = 0.0f;
        }
        if (v > 1.0f) {
            v = 1.0f;
        }

        v = std::pow(v, cfg_.gamma);

        const int display_bucket = applyVerticalOffset(natural_bucket, meta.vertical_offset_rows);
        const int y = bucketToY(display_bucket);

        QRgb *row = reinterpret_cast<QRgb *>(image_->scanLine(y));
        row[x] = lerpColor(cfg_.background_color, cfg_.sound_color, v);
    }

    rendered_columns_[static_cast<std::size_t>(x)] = meta;
    reapplyMarkersForColumn(x);
}

void SoundImageRenderer::renderCurrentColumnToImage()
{
    if (!center_locked_ || !have_active_column_) {
        return;
    }

    RenderedColumn meta;
    meta.valid = true;
    meta.column_index = active_column_index_;
    meta.start_sample = active_start_sample_;
    meta.end_sample = active_end_sample_;
    meta.vertical_offset_rows = internal_vertical_offset_rows_;

    renderBinsToColumn(write_column_, current_column_.data(), meta);
}

/*
    commitAnchorColumn()
    --------------------
    Buffers one completed post-warmup column while automatic centering is still
    being computed.

    Once enough anchor columns are collected, the dominant vertical band is found
    and shifted toward the middle of the image.
*/
void SoundImageRenderer::commitAnchorColumn(const float *column,
                                            const AnchorColumn &meta)
{
    if (!column || anchor_used_ >= cfg_.anchor_columns) {
        return;
    }

    float *dst = &anchor_columns_buffer_[static_cast<std::size_t>(anchor_used_ * height_)];
    std::copy(column, column + height_, dst);
    anchor_columns_meta_[static_cast<std::size_t>(anchor_used_)] = meta;

    for (int i = 0; i < height_; ++i) {
        anchor_sum_[static_cast<std::size_t>(i)] += column[i];
    }

    ++anchor_used_;

    if (anchor_used_ == cfg_.anchor_columns) {
        const int dominant_bucket = argmaxSmoothed5(anchor_sum_);
        internal_vertical_offset_rows_ = (height_ / 2) - dominant_bucket;
        center_locked_ = true;
        flushBufferedAnchorColumns();
    }
}

void SoundImageRenderer::flushBufferedAnchorColumns()
{
    if (!center_locked_) {
        return;
    }

    for (int c = 0; c < cfg_.anchor_columns; ++c) {
        const AnchorColumn &am = anchor_columns_meta_[static_cast<std::size_t>(c)];
        if (!am.valid) {
            continue;
        }

        const float *bins = &anchor_columns_buffer_[static_cast<std::size_t>(c * height_)];

        RenderedColumn meta;
        meta.valid = true;
        meta.column_index = am.column_index;
        meta.start_sample = am.start_sample;
        meta.end_sample = am.end_sample;
        meta.vertical_offset_rows = internal_vertical_offset_rows_;

        renderBinsToColumn(write_column_, bins, meta);

        last_completed_column_ = write_column_;
        write_column_ = (write_column_ + 1) % width_;

        clearRenderedColumnStorage(write_column_);
        rebuildColumnNeighborhood(write_column_);
    }

    clearCurrentBuckets();
    pruneOldMarkers();
}

void SoundImageRenderer::rebuildColumnNeighborhood(int center_column)
{
    /*
        Marker-bleed cleanup
        --------------------
        A marker is not always confined to its center column. For example, a
        9-by-9 marker has a horizontal radius of 4 columns. If column X wraps and
        is reused, an old marker that used to touch X may also have left pixels
        in X-1, X+1, etc.

        Clearing only X is therefore insufficient.

        This function rebuilds a small neighborhood around the reused column:
            1) clear all columns in the neighborhood
            2) redraw the still-valid sound columns from rendered_bins_
            3) reapply markers column-by-column

        Since reapplyMarkersForColumn(column) only draws the portion of each
        marker that belongs in that specific column, the final overlay is clean
        and deterministic.
    */
    if (!image_ || center_column < 0 || center_column >= width_) {
        return;
    }

    const int radius = maxSupportedMarkerRadiusColumns();

    const int x0 = std::max(0, center_column - radius);
    const int x1 = std::min(width_ - 1, center_column + radius);

    for (int x = x0; x <= x1; ++x) {
        clearColumn(x, cfg_.background_color);
    }

    for (int x = x0; x <= x1; ++x) {
        const RenderedColumn &meta = rendered_columns_[static_cast<std::size_t>(x)];
        if (!meta.valid) {
            continue;
        }

        const float *bins = renderedBinsPtr(x);
        if (!bins) {
            continue;
        }

        renderBinsToColumn(x, bins, meta);
    }
}

/*
    finalizeCurrentColumnAndAdvance()
    ---------------------------------
    Handles a completed active column.

    Stages:
        1. warmup columns are ignored
        2. anchor columns are buffered until center offset is locked
        3. normal columns are rendered to the circular image

    After a screen column is consumed, the next screen column is invalidated and
    its marker-bleed neighborhood is rebuilt.
*/
void SoundImageRenderer::finalizeCurrentColumnAndAdvance()
{
    if (!have_active_column_) {
        return;
    }

    AnchorColumn completed;
    completed.valid = true;
    completed.column_index = active_column_index_;
    completed.start_sample = active_start_sample_;
    completed.end_sample = active_end_sample_;

    if (warmup_columns_consumed_ < cfg_.warmup_columns) {
        ++warmup_columns_consumed_;
        clearCurrentBuckets();
        pruneOldMarkers();
        return;
    }

    if (!center_locked_) {
        commitAnchorColumn(current_column_.data(), completed);
        clearCurrentBuckets();
        pruneOldMarkers();
        return;
    }

    RenderedColumn meta;
    meta.valid = true;
    meta.column_index = completed.column_index;
    meta.start_sample = completed.start_sample;
    meta.end_sample = completed.end_sample;
    meta.vertical_offset_rows = internal_vertical_offset_rows_;

    renderBinsToColumn(write_column_, current_column_.data(), meta);

    last_completed_column_ = write_column_;
    write_column_ = (write_column_ + 1) % width_;

    clearRenderedColumnStorage(write_column_);
    rebuildColumnNeighborhood(write_column_);

    clearCurrentBuckets();
    pruneOldMarkers();
}

/*
    processSamples()
    ----------------
    Main streaming hot path.

    Before BPH:
        Count samples only.

    After BPH:
        For each sample:
            - start a column if needed
            - finalize columns crossed by this sample
            - remove DC
            - rectify magnitude
            - normalize by peak envelope
            - update the strongest value in the proper bucket
            - advance the absolute sample counter
*/
void SoundImageRenderer::processSamples(const float *samples, std::size_t count)
{
    if (!image_ || !samples || count == 0) {
        return;
    }

    if (!bph_valid_) {
        processed_samples_since_reset_ += static_cast<quint64>(count);
        return;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const quint64 abs_index = stream_origin_sample_index_ + processed_samples_since_reset_;

        if (!have_active_column_) {
            startActiveColumn(0);
        }

        while (abs_index >= active_end_sample_) {
            finalizeCurrentColumnAndAdvance();
            startActiveColumn(active_column_index_ + 1);
        }

        const float x = samples[i];

        dc_mean_ += dc_alpha_ * (static_cast<double>(x) - dc_mean_);
        const float dc_removed = x - static_cast<float>(dc_mean_);
        const float mag = std::fabs(dc_removed);

        if (mag > peak_env_) {
            peak_env_ = mag;
        } else {
            peak_env_ *= cfg_.peak_decay;
            if (peak_env_ < 1.0e-6f) {
                peak_env_ = 1.0e-6f;
            }
        }

        float z = mag / peak_env_;
        if (z > 1.0f) {
            z = 1.0f;
        }

        const int natural_bucket =
            sampleToBucketInRange(abs_index, active_start_sample_, active_end_sample_);

        if (z > current_column_[static_cast<std::size_t>(natural_bucket)]) {
            current_column_[static_cast<std::size_t>(natural_bucket)] = z;
        }

        ++processed_samples_since_reset_;
    }

    if (cfg_.live_preview_current_column && center_locked_ && have_active_column_) {
        renderCurrentColumnToImage();
    }
}

/*
    lookupRenderedColumnBySampleIndex()
    -----------------------------------
    Finds which visible column owns the supplied absolute sample index.

    This is deterministic range lookup, not heuristic searching.

    A column owns sample N if:
        start_sample <= N < end_sample
*/
int SoundImageRenderer::lookupRenderedColumnBySampleIndex(quint64 absolute_sample_index) const
{
    for (int x = 0; x < width_; ++x) {
        const RenderedColumn &meta = rendered_columns_[static_cast<std::size_t>(x)];
        if (!meta.valid) {
            continue;
        }

        if (absolute_sample_index >= meta.start_sample &&
            absolute_sample_index < meta.end_sample) {
            return x;
        }
    }

    return -1;
}

/*
    mapRenderedSampleToPixel()
    --------------------------
    Converts an absolute sample index to an image pixel by using the metadata of
    the column that actually rendered that sample.

    This is the marker alignment guarantee:
        marker sample N maps through the same column range as signal sample N.
*/
bool SoundImageRenderer::mapRenderedSampleToPixel(quint64 absolute_sample_index,
                                                  int *out_x,
                                                  int *out_y) const
{
    const int x = lookupRenderedColumnBySampleIndex(absolute_sample_index);
    if (x < 0) {
        if (out_x) {
            *out_x = -1;
        }
        if (out_y) {
            *out_y = -1;
        }
        return false;
    }

    const RenderedColumn &meta = rendered_columns_[static_cast<std::size_t>(x)];

    const int natural_bucket =
        sampleToBucketInRange(absolute_sample_index, meta.start_sample, meta.end_sample);
    const int display_bucket = applyVerticalOffset(natural_bucket, meta.vertical_offset_rows);
    const int y = bucketToY(display_bucket);

    if (out_x) {
        *out_x = x;
    }
    if (out_y) {
        *out_y = y;
    }

    return true;
}

void SoundImageRenderer::drawCenteredMarkerBlock(int x, int y, QRgb color, int marker_side_pixels)
{
    if (!image_) {
        return;
    }

    const int side = normalizeMarkerSidePixels(marker_side_pixels);
    const int radius = side / 2;

    for (int dy = -radius; dy <= radius; ++dy) {
        const int yy = y + dy;
        if (yy < 0 || yy >= height_) {
            continue;
        }

        QRgb *row = reinterpret_cast<QRgb *>(image_->scanLine(yy));
        for (int dx = -radius; dx <= radius; ++dx) {
            const int xx = x + dx;
            if (xx < 0 || xx >= width_) {
                continue;
            }
            row[xx] = color;
        }
    }
}

void SoundImageRenderer::drawCenteredMarkerContributionToColumn(int target_column,
                                                                int center_x,
                                                                int center_y,
                                                                QRgb color,
                                                                int marker_side_pixels)
{
    if (!image_ || target_column < 0 || target_column >= width_) {
        return;
    }

    const int side = normalizeMarkerSidePixels(marker_side_pixels);
    const int radius = side / 2;

    if (target_column < center_x - radius || target_column > center_x + radius) {
        return;
    }

    for (int dy = -radius; dy <= radius; ++dy) {
        const int yy = center_y + dy;
        if (yy < 0 || yy >= height_) {
            continue;
        }

        QRgb *row = reinterpret_cast<QRgb *>(image_->scanLine(yy));
        row[target_column] = color;
    }
}

void SoundImageRenderer::reapplyMarkersForColumn(int column)
{
    if (!image_ || column < 0 || column >= width_) {
        return;
    }

    for (const Marker &m : active_markers_) {
        int cx = -1;
        int cy = -1;

        if (!mapRenderedSampleToPixel(m.absolute_sample_index, &cx, &cy)) {
            continue;
        }

        drawCenteredMarkerContributionToColumn(column, cx, cy, m.color, m.side);
    }
}

void SoundImageRenderer::addPersistentMarkerFromAbsoluteSample(quint64 absolute_sample_index,
                                                               QRgb color,
                                                               int marker_side_pixels)
{
    /*
        Markers are stored persistently so they can be redrawn after sound
        columns are refreshed.

        Important:
            absolute_sample_index is not adjusted here. The caller must pass the
            original event sample index in the same clock used by processSamples().
    */
    Marker m;
    m.absolute_sample_index = absolute_sample_index;
    m.color = color;
    m.side = normalizeMarkerSidePixels(marker_side_pixels);
    active_markers_.push_back(m);

    int x = -1;
    int y = -1;
    if (mapRenderedSampleToPixel(absolute_sample_index, &x, &y)) {
        drawCenteredMarkerBlock(x, y, color, m.side);
    }
}

/*
    pruneOldMarkers()
    -----------------
    Removes marker entries that are older than the oldest visible column.

    This keeps active_markers_ bounded during long-running streams.
*/
void SoundImageRenderer::pruneOldMarkers()
{
    quint64 oldest_visible = 0;
    bool have_visible = false;

    for (const RenderedColumn &meta : rendered_columns_) {
        if (!meta.valid) {
            continue;
        }

        if (!have_visible || meta.start_sample < oldest_visible) {
            oldest_visible = meta.start_sample;
            have_visible = true;
        }
    }

    if (!have_visible) {
        return;
    }

    auto it = std::remove_if(
        active_markers_.begin(),
        active_markers_.end(),
        [oldest_visible](const Marker &m) {
            return m.absolute_sample_index < oldest_visible;
        });

    active_markers_.erase(it, active_markers_.end());
}

void SoundImageRenderer::markAEventAbsoluteSampleIndex(quint64 absolute_sample_index,
                                                       QRgb color,
                                                       int marker_side_pixels)
{
    addPersistentMarkerFromAbsoluteSample(absolute_sample_index, color, marker_side_pixels);
}

void SoundImageRenderer::markCEventAbsoluteSampleIndex(quint64 absolute_sample_index,
                                                       QRgb color,
                                                       int marker_side_pixels)
{
    addPersistentMarkerFromAbsoluteSample(absolute_sample_index, color, marker_side_pixels);
}

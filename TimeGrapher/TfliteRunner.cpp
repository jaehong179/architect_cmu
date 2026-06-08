#include "TfliteRunner.h"

#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <QDebug>

// ============================================================
// FlatBuffer binary parsing helpers
// ============================================================

namespace {

static uint32_t fb_u32(const uint8_t* d, size_t p)
{
    uint32_t v; memcpy(&v, d + p, 4); return v;
}
static int32_t fb_s32(const uint8_t* d, size_t p)
{
    int32_t v; memcpy(&v, d + p, 4); return v;
}
static int8_t fb_s8(const uint8_t* d, size_t p)
{
    return static_cast<int8_t>(d[p]);
}
static float fb_f32(const uint8_t* d, size_t p)
{
    float v; memcpy(&v, d + p, 4); return v;
}
static uint16_t fb_u16(const uint8_t* d, size_t p)
{
    uint16_t v; memcpy(&v, d + p, 2); return v;
}
static int64_t fb_s64(const uint8_t* d, size_t p)
{
    int64_t v; memcpy(&v, d + p, 8); return v;
}

// Absolute position of field data inside a table (0 = absent)
static size_t fb_field(const uint8_t* d, size_t table, int field_idx)
{
    int32_t vt_off = fb_s32(d, table);
    size_t vtable = static_cast<size_t>(static_cast<int64_t>(table) - vt_off);
    uint16_t vt_size = fb_u16(d, vtable);
    size_t slot = vtable + 4 + static_cast<size_t>(field_idx) * 2;
    if (slot + 2 > vtable + vt_size) return 0;
    uint16_t foff = fb_u16(d, slot);
    return foff ? table + foff : 0;
}

// Follow a UOffset stored at pos → absolute position of referenced object
static size_t fb_deref(const uint8_t* d, size_t pos)
{
    return pos + fb_u32(d, pos);
}

// Get the vector (or table) pointed to by a reference field
static size_t fb_field_ref(const uint8_t* d, size_t table, int field_idx)
{
    size_t fp = fb_field(d, table, field_idx);
    return fp ? fb_deref(d, fp) : 0;
}

static uint32_t fb_vec_len(const uint8_t* d, size_t vec)
{
    return vec ? fb_u32(d, vec) : 0;
}
static int32_t fb_vec_s32(const uint8_t* d, size_t vec, uint32_t i)
{
    return fb_s32(d, vec + 4 + i * 4);
}
static float fb_vec_f32(const uint8_t* d, size_t vec, uint32_t i)
{
    return fb_f32(d, vec + 4 + i * 4);
}
// i-th element of a reference-vector (array of tables/vectors)
static size_t fb_vec_table(const uint8_t* d, size_t vec, uint32_t i)
{
    size_t ep = vec + 4 + i * 4;
    return fb_deref(d, ep);
}
static const uint8_t* fb_vec_bytes(const uint8_t* d, size_t vec)
{
    return d + vec + 4;
}

} // anonymous namespace

// ============================================================
// Parsed model data structures
// ============================================================

struct ParsedTensor {
    std::vector<int> shape;
    int  type        = 0;  // 0=FLOAT32, 9=INT8
    float scale      = 0.0f;
    int64_t zeroPoint = 0;
    // Pre-dequantized float weights (populated at load time for weight tensors)
    std::vector<float> weights;
};

struct ParsedOp {
    int              opcode = -1;
    std::vector<int> inputs;
    std::vector<int> outputs;
};

struct ParsedModel {
    std::vector<ParsedTensor> tensors;
    std::vector<ParsedOp>     ops;
    int graphInput  = -1;
    int graphOutput = -1;
};

// ============================================================
// Model parsing
// ============================================================

static ParsedModel parseModel(const uint8_t* buf, size_t /*size*/)
{
    ParsedModel model;
    size_t root = fb_u32(buf, 0);

    // --- Collect raw buffer data pointers ---
    size_t bufs_vec = fb_field_ref(buf, root, 4);
    uint32_t num_bufs = fb_vec_len(buf, bufs_vec);
    std::vector<const uint8_t*> buf_ptr(num_bufs, nullptr);
    std::vector<size_t>         buf_len(num_bufs, 0);
    for (uint32_t bi = 0; bi < num_bufs; bi++) {
        size_t bt   = fb_vec_table(buf, bufs_vec, bi);
        size_t dfp  = fb_field(buf, bt, 0);
        if (dfp) {
            size_t v      = fb_deref(buf, dfp);
            buf_len[bi]   = fb_vec_len(buf, v);
            buf_ptr[bi]   = buf_len[bi] ? fb_vec_bytes(buf, v) : nullptr;
        }
    }

    // --- First subgraph ---
    size_t sgs_vec = fb_field_ref(buf, root, 2);
    size_t sg      = fb_vec_table(buf, sgs_vec, 0);

    // --- Tensors ---
    size_t tens_vec  = fb_field_ref(buf, sg, 0);
    uint32_t num_ten = fb_vec_len(buf, tens_vec);
    model.tensors.resize(num_ten);

    for (uint32_t ti = 0; ti < num_ten; ti++) {
        size_t t = fb_vec_table(buf, tens_vec, ti);
        ParsedTensor& pt = model.tensors[ti];

        size_t shp_v = fb_field_ref(buf, t, 0);
        for (uint32_t d = 0; d < fb_vec_len(buf, shp_v); d++)
            pt.shape.push_back(fb_vec_s32(buf, shp_v, d));

        // type is a byte enum
        size_t type_fp = fb_field(buf, t, 1);
        pt.type = type_fp ? static_cast<int>(buf[type_fp]) : 0;

        size_t buf_fp   = fb_field(buf, t, 2);
        uint32_t buf_idx = buf_fp ? fb_u32(buf, buf_fp) : 0;

        // Quantization params (field 4)
        size_t q_fp = fb_field(buf, t, 4);
        if (q_fp) {
            size_t quant    = fb_deref(buf, q_fp);
            size_t sc_v     = fb_field_ref(buf, quant, 2);
            if (fb_vec_len(buf, sc_v) > 0)
                pt.scale = fb_vec_f32(buf, sc_v, 0);
            size_t zp_v = fb_field_ref(buf, quant, 3);
            if (fb_vec_len(buf, zp_v) > 0)
                pt.zeroPoint = fb_s64(buf, zp_v + 4);
        }

        // Pre-load weights into float vector
        if (buf_idx < num_bufs && buf_ptr[buf_idx] && buf_len[buf_idx] > 0) {
            const uint8_t* src  = buf_ptr[buf_idx];
            size_t          bsz = buf_len[buf_idx];
            if (pt.type == 9) { // INT8 – dequantize
                size_t n = bsz;
                pt.weights.resize(n);
                float scale = pt.scale;
                int8_t zp   = static_cast<int8_t>(pt.zeroPoint);
                const int8_t* isrc = reinterpret_cast<const int8_t*>(src);
                for (size_t k = 0; k < n; k++)
                    pt.weights[k] = (isrc[k] - zp) * scale;
            } else { // FLOAT32
                size_t n = bsz / 4;
                pt.weights.resize(n);
                memcpy(pt.weights.data(), src, n * 4);
            }
        }
    }

    // --- Operators ---
    size_t ops_vec   = fb_field_ref(buf, sg, 3);
    uint32_t num_ops = fb_vec_len(buf, ops_vec);
    size_t oc_vec    = fb_field_ref(buf, root, 1); // operator_codes

    model.ops.resize(num_ops);
    for (uint32_t oi = 0; oi < num_ops; oi++) {
        size_t op = fb_vec_table(buf, ops_vec, oi);
        ParsedOp& po = model.ops[oi];

        size_t oc_fp     = fb_field(buf, op, 0);
        uint32_t oc_idx  = oc_fp ? fb_u32(buf, oc_fp) : 0;
        size_t oc_table  = fb_vec_table(buf, oc_vec, oc_idx);

        // builtin_code: field 3 (int32, newer) or field 0 (byte, deprecated)
        size_t bc3 = fb_field(buf, oc_table, 3);
        if (bc3)
            po.opcode = fb_s32(buf, bc3);
        else {
            size_t bc0 = fb_field(buf, oc_table, 0);
            po.opcode  = bc0 ? static_cast<int>(fb_s8(buf, bc0)) : -1;
        }

        size_t in_v  = fb_field_ref(buf, op, 1);
        size_t out_v = fb_field_ref(buf, op, 2);
        for (uint32_t ii = 0; ii < fb_vec_len(buf, in_v);  ii++)
            po.inputs.push_back(fb_vec_s32(buf, in_v,  ii));
        for (uint32_t io = 0; io < fb_vec_len(buf, out_v); io++)
            po.outputs.push_back(fb_vec_s32(buf, out_v, io));
    }

    // --- Graph I/O ---
    size_t gi_v = fb_field_ref(buf, sg, 1);
    size_t go_v = fb_field_ref(buf, sg, 2);
    if (fb_vec_len(buf, gi_v) > 0) model.graphInput  = fb_vec_s32(buf, gi_v, 0);
    if (fb_vec_len(buf, go_v) > 0) model.graphOutput = fb_vec_s32(buf, go_v, 0);

    return model;
}

// ============================================================
// Layer implementations
// ============================================================

// Conv2D: SAME padding, stride (1,1)
// kernel layout: [out_ch, kH, kW, in_ch]  (TFLite OHWI)
static std::vector<float> conv2d(
    const float* input, int H, int W, int in_ch,
    const float* kernel, int out_ch, int kH, int kW,
    const float* bias,   // nullable
    bool relu)
{
    int pH = kH / 2;
    int pW = kW / 2;
    std::vector<float> out(static_cast<size_t>(out_ch * H * W), 0.0f);

    for (int oc = 0; oc < out_ch; oc++) {
        float b = bias ? bias[oc] : 0.0f;
        for (int oh = 0; oh < H; oh++) {
            for (int ow = 0; ow < W; ow++) {
                float sum = b;
                for (int kh = 0; kh < kH; kh++) {
                    int ih = oh + kh - pH;
                    if (ih < 0 || ih >= H) continue;
                    for (int kw = 0; kw < kW; kw++) {
                        int iw = ow + kw - pW;
                        if (iw < 0 || iw >= W) continue;
                        const float* inp_row = input + ih * W * in_ch + iw * in_ch;
                        const float* ker_row = kernel + oc * kH * kW * in_ch
                                                       + kh * kW * in_ch
                                                       + kw * in_ch;
                        for (int ic = 0; ic < in_ch; ic++)
                            sum += inp_row[ic] * ker_row[ic];
                    }
                }
                if (relu && sum < 0.0f) sum = 0.0f;
                out[oh * W * out_ch + ow * out_ch + oc] = sum;
            }
        }
    }
    return out;
}

// MaxPool2D: VALID padding, pool (pH, pW), stride (sH, sW)
static std::vector<float> maxpool2d(
    const float* input, int H, int W, int ch,
    int pH, int pW, int sH, int sW)
{
    int outH = (H - pH) / sH + 1;
    int outW = (W - pW) / sW + 1;
    std::vector<float> out(static_cast<size_t>(outH * outW * ch));

    for (int oh = 0; oh < outH; oh++) {
        for (int ow = 0; ow < outW; ow++) {
            for (int c = 0; c < ch; c++) {
                float maxVal = -1e38f;
                for (int kh = 0; kh < pH; kh++) {
                    for (int kw = 0; kw < pW; kw++) {
                        int ih = oh * sH + kh;
                        int iw = ow * sW + kw;
                        float v = input[ih * W * ch + iw * ch + c];
                        if (v > maxVal) maxVal = v;
                    }
                }
                out[oh * outW * ch + ow * ch + c] = maxVal;
            }
        }
    }
    return out;
}

// Fully-connected: weights layout [out, in]
static std::vector<float> fullyConnected(
    const float* input, int in_size,
    const float* weights, int out_size,
    const float* bias,   // nullable
    bool relu)
{
    std::vector<float> out(static_cast<size_t>(out_size));
    for (int o = 0; o < out_size; o++) {
        float sum = bias ? bias[o] : 0.0f;
        const float* row = weights + o * in_size;
        for (int i = 0; i < in_size; i++)
            sum += input[i] * row[i];
        if (relu && sum < 0.0f) sum = 0.0f;
        out[o] = sum;
    }
    return out;
}

// Softmax over 1-D vector
static std::vector<float> softmax(const float* input, int size)
{
    std::vector<float> out(static_cast<size_t>(size));
    float maxv = *std::max_element(input, input + size);
    float sum  = 0.0f;
    for (int i = 0; i < size; i++) { out[i] = std::exp(input[i] - maxv); sum += out[i]; }
    for (int i = 0; i < size; i++) out[i] /= sum;
    return out;
}

// ============================================================
// TfliteRunner implementation
// ============================================================

struct TfliteRunner::Impl {
    std::vector<uint8_t> fileData;
    ParsedModel          model;
};

TfliteRunner::TfliteRunner(const std::string& modelPath)
    : mImpl(std::make_unique<Impl>())
{
    std::ifstream file(modelPath, std::ios::binary | std::ios::ate);
    if (!file) {
        qWarning() << "TfliteRunner: cannot open" << modelPath.c_str();
        return;
    }
    auto sz = file.tellg();
    if (sz <= 0) {
        qWarning() << "TfliteRunner: empty file" << modelPath.c_str();
        return;
    }
    file.seekg(0);
    mImpl->fileData.resize(static_cast<size_t>(sz));
    file.read(reinterpret_cast<char*>(mImpl->fileData.data()), sz);

    try {
        mImpl->model = parseModel(mImpl->fileData.data(), static_cast<size_t>(sz));
    } catch (const std::exception& e) {
        qWarning() << "TfliteRunner: parse error:" << e.what();
        return;
    }

    qInfo() << "TfliteRunner: loaded" << modelPath.c_str()
            << "tensors=" << mImpl->model.tensors.size()
            << "ops="     << mImpl->model.ops.size();
    mValid = true;
}

TfliteRunner::~TfliteRunner() = default;

std::vector<float> TfliteRunner::run(const std::vector<float>& input) const
{
    if (!mValid) return {};

    // Expected model layout (indices confirmed by parsing mfcc_cnn.tflite):
    //  tensors:  0=input  1=conv1_k  2=conv1_b  3=conv2_b  7=fc2_w
    //            8=conv2_k(INT8)  9=fc1_w(INT8)
    //  ops:  CONV_2D(0,1,2→10)  MAXPOOL(10→11)  CONV_2D(11,8,3→12)
    //        MAXPOOL(12→13)  [shape/slice/pack/reshape: 13→17]
    //        FULLY_CONNECTED(17,9→18, relu)  FULLY_CONNECTED(18,7→19)
    //        SOFTMAX(19→20)

    const auto& T = mImpl->model.tensors;

    // Validate input size: [1, 49, 13, 1] = 637
    if (input.size() != 49 * 13) {
        qWarning() << "TfliteRunner: expected 637 floats, got" << input.size();
        return {};
    }

    // ---- Op 0: Conv2D  input[1,49,13,1] → [1,49,13,8]  (relu fused) ----
    if (T[1].weights.empty() || T[2].weights.empty()) {
        qWarning() << "TfliteRunner: missing conv1 weights";
        return {};
    }
    auto conv1_out = conv2d(
        input.data(), 49, 13, 1,
        T[1].weights.data(), 8, 3, 3,
        T[2].weights.data(),
        /*relu=*/true);

    // ---- Op 1: MaxPool2D [1,49,13,8] → [1,24,6,8] ----
    auto pool1_out = maxpool2d(
        conv1_out.data(), 49, 13, 8,
        /*pH=*/2, /*pW=*/2, /*sH=*/2, /*sW=*/2);

    // ---- Op 2: Conv2D [1,24,6,8] → [1,24,6,16]  (relu fused, INT8 kernel) ----
    if (T[8].weights.empty() || T[3].weights.empty()) {
        qWarning() << "TfliteRunner: missing conv2 weights";
        return {};
    }
    auto conv2_out = conv2d(
        pool1_out.data(), 24, 6, 8,
        T[8].weights.data(), 16, 3, 3,
        T[3].weights.data(),
        /*relu=*/true);

    // ---- Op 3: MaxPool2D [1,24,6,16] → [1,12,3,16] ----
    auto pool2_out = maxpool2d(
        conv2_out.data(), 24, 6, 16,
        /*pH=*/2, /*pW=*/2, /*sH=*/2, /*sW=*/2);

    // ---- Ops 4-7: Flatten [1,12,3,16] → [1,576] ----
    // (SHAPE/STRIDED_SLICE/PACK/RESHAPE; result is just a reinterpretation)
    const float* flat = pool2_out.data(); // 12*3*16 = 576 elements

    // ---- Op 8: FC [1,576] → [1,32]  (relu fused, INT8 weights) ----
    if (T[9].weights.empty()) {
        qWarning() << "TfliteRunner: missing fc1 weights";
        return {};
    }
    auto fc1_out = fullyConnected(
        flat, 576,
        T[9].weights.data(), 32,
        /*bias=*/nullptr,
        /*relu=*/true);

    // ---- Op 9: FC [1,32] → [1,4]  (no activation before softmax) ----
    if (T[7].weights.empty()) {
        qWarning() << "TfliteRunner: missing fc2 weights";
        return {};
    }
    auto fc2_out = fullyConnected(
        fc1_out.data(), 32,
        T[7].weights.data(), 4,
        /*bias=*/nullptr,
        /*relu=*/false);

    // ---- Op 10: Softmax [1,4] → [1,4] ----
    return softmax(fc2_out.data(), 4);
}

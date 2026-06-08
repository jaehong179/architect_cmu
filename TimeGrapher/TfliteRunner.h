#pragma once

#include <string>
#include <vector>
#include <memory>

/*
 * TfliteRunner – Self-contained TFLite inference runner.
 *
 * Parses a .tflite FlatBuffer file (no external TFLite library required) and
 * executes the mfcc_cnn model architecture:
 *   Input  (1, 49, 13, 1) float32
 *   Conv2D(8,  3x3, relu, same)
 *   MaxPool2D(2, 2)
 *   Conv2D(16, 3x3, relu, same)
 *   MaxPool2D(2, 2)
 *   Flatten
 *   Dense(32, relu)
 *   Dense(4)
 *   Softmax
 *   Output (1, 4) float32
 *
 * Supports FLOAT32 and INT8 (dynamic-range quantized) weight tensors.
 */
class TfliteRunner
{
public:
    explicit TfliteRunner(const std::string& modelPath);
    ~TfliteRunner();

    // Run inference. input must be exactly numFrames * numCoeffs elements,
    // laid out as [frame0_coeff0, frame0_coeff1, ..., frame48_coeff12].
    // Returns a 4-element vector of class probabilities on success,
    // or an empty vector on failure.
    std::vector<float> run(const std::vector<float>& input) const;

    bool isValid() const { return mValid; }

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    bool mValid = false;
};

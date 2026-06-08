import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers
import numpy as np

# ---- 하이퍼파라미터 / 입력 형태 정의 ----
sample_rate = 16000         # 참고용, 여기선 직접 쓰진 않음
n_mfcc = 13
num_frames = 49
num_classes = 4             # 원하는 클래스 수로 변경

input_shape = (num_frames, n_mfcc, 1)

# ---- Keras 모델 정의 ----
inputs = keras.Input(shape=input_shape, name="mfcc_input")

x = layers.Conv2D(8, (3, 3), activation="relu", padding="same")(inputs)
x = layers.MaxPooling2D((2, 2))(x)
x = layers.Conv2D(16, (3, 3), activation="relu", padding="same")(x)
x = layers.MaxPooling2D((2, 2))(x)
x = layers.Flatten()(x)
x = layers.Dense(32, activation="relu")(x)
outputs = layers.Dense(num_classes, activation="softmax", name="output")(x)

model = keras.Model(inputs=inputs, outputs=outputs)

# 컴파일(추후 학습 시 필요, 지금은 생략 가능)
model.compile(
    optimizer="adam",
    loss="sparse_categorical_crossentropy",
    metrics=["accuracy"],
)

# 구조 확인
model.summary()


# ---- TFLite Converter 설정 및 변환 ----
converter = tf.lite.TFLiteConverter.from_keras_model(model)

# 기본 최적화 (양자화 등의 최적화는 나중에 추가 가능)
converter.optimizations = [tf.lite.Optimize.DEFAULT]

tflite_model = converter.convert()

# 파일로 저장 (라즈베리파이에 이 파일을 복사해서 사용)
tflite_model_path = "mfcc_cnn.tflite"
with open(tflite_model_path, "wb") as f:
    f.write(tflite_model)

print("TFLite model size (bytes):", len(tflite_model))
print("Saved to:", tflite_model_path)


# ---- Full INT8 Quantization ----
def representative_dataset():
    for _ in range(100):
        sample = np.random.randn(1, *input_shape).astype(np.float32)
        yield [sample]


converter_int8 = tf.lite.TFLiteConverter.from_keras_model(model)
converter_int8.optimizations = [tf.lite.Optimize.DEFAULT]
converter_int8.representative_dataset = representative_dataset
converter_int8.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter_int8.inference_input_type = tf.int8
converter_int8.inference_output_type = tf.int8

tflite_int8_model = converter_int8.convert()

tflite_int8_path = "mfcc_cnn_int8.tflite"
with open(tflite_int8_path, "wb") as f:
    f.write(tflite_int8_model)

print("\nFull INT8 Quantized model size (bytes):", len(tflite_int8_model))
print("Saved to:", tflite_int8_path)
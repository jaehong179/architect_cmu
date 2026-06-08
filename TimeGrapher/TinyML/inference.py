import time
import numpy as np
import tensorflow as tf
import librosa
import os

# GPU 비활성화, CPU만 사용
os.environ["CUDA_VISIBLE_DEVICES"] = "-1"
tf.config.set_visible_devices([], 'GPU')

# ---- 파라미터 ----
sample_rate = 192000
n_mfcc = 13
num_frames = 49  # 모델이 기대하는 프레임 수

tflite_model_path = "mfcc_cnn.tflite"

# ---- 1) 모델 로드 ----
t0 = time.time()
interpreter = tf.lite.Interpreter(model_path=tflite_model_path)
interpreter.allocate_tensors()
t1 = time.time()

load_time_ms = (t1 - t0) * 1000.0
print(f"Model load + allocate_tensors time: {load_time_ms:.2f} ms")

input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

# ---- 2) 1초 길이 오디오 생성 (192kHz, 테스트용 더미 데이터) ----
# 실제 WAV 파일이 없으므로 임의의 raw 데이터 생성
audio = np.random.randn(sample_rate).astype(np.float32)  # 1초 = 192000 샘플
np.tofile = audio.tofile("input.raw")
print(f"Generated input.raw: {len(audio)} samples ({len(audio)/sample_rate:.1f}s at {sample_rate}Hz)")

# 실제 WAV 파일 사용 시 아래로 교체:
# audio, sr = librosa.load("input.wav", sr=sample_rate, duration=1.0)

# ---- 3) MFCC 추출 ----
t2 = time.time()
mfcc = librosa.feature.mfcc(y=audio, sr=sample_rate, n_mfcc=n_mfcc)
# mfcc shape: (n_mfcc, time_frames)

# 프레임 수를 모델 입력에 맞게 조정 (패딩 또는 자르기)
if mfcc.shape[1] < num_frames:
    pad_width = num_frames - mfcc.shape[1]
    mfcc = np.pad(mfcc, ((0, 0), (0, pad_width)), mode='constant')
else:
    mfcc = mfcc[:, :num_frames]

# (num_frames, n_mfcc, 1) 형태로 변환
mfcc_input = mfcc.T[np.newaxis, :, :, np.newaxis].astype(np.float32)
t3 = time.time()

print(f"MFCC extraction time: {(t3 - t2) * 1000:.2f} ms")
print(f"MFCC input shape: {mfcc_input.shape}")

# ---- 4) Inference ----
start = time.time()
interpreter.set_tensor(input_details[0]['index'], mfcc_input)
interpreter.invoke()
end = time.time()

output_data = interpreter.get_tensor(output_details[0]['index'])
inference_ms = (end - start) * 1000.0

print(f"Inference time: {inference_ms:.4f} ms")
print(f"Output probabilities: {output_data[0]}")
print(f"Predicted class: {np.argmax(output_data[0])}")
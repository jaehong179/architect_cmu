#!/usr/bin/env bash
# run_batch.sh — 폴더 안의 모든 WAV 파일에 rate_error_cli 를 돌려, WAV 파일과 같은 이름의 CSV로 남긴다.
#  사용: tools/rate_error_cli/run_batch.sh <wav_dir> [out_dir] [-- <rate_error_cli 옵션...>]
#   wav_dir 의 *.wav 각각에 대해 out_dir/<같은이름>.csv 생성(out_dir 기본값 = wav_dir).
#   바이너리가 없으면(최초 1회) 이 스크립트가 cmake 로 자동 빌드한다.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$SCRIPT_DIR/build/rate_error_cli"

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <wav_dir> [out_dir] [-- <rate_error_cli options>]" >&2
    exit 1
fi

WAV_DIR="$1"; shift
OUT_DIR="$WAV_DIR"
if [[ $# -gt 0 && "$1" != "--" ]]; then OUT_DIR="$1"; shift; fi
if [[ $# -gt 0 && "$1" == "--" ]]; then shift; fi
EXTRA_ARGS=("$@")

if [[ ! -d "$WAV_DIR" ]]; then
    echo "error: not a directory: $WAV_DIR" >&2
    exit 1
fi

if [[ ! -x "$BIN" ]]; then
    echo "rate_error_cli 바이너리가 없어 먼저 빌드합니다..." >&2
    cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build" >/dev/null
    cmake --build "$SCRIPT_DIR/build" -j"$(nproc)" >/dev/null
fi

mkdir -p "$OUT_DIR"

shopt -s nullglob nocaseglob
wavs=("$WAV_DIR"/*.wav)
shopt -u nocaseglob nullglob
if [[ ${#wavs[@]} -eq 0 ]]; then
    echo "no .wav files found in $WAV_DIR" >&2
    exit 1
fi

ok=0 fail=0
for wav in "${wavs[@]}"; do
    base="$(basename "$wav")"
    csv="$OUT_DIR/${base%.*}.csv"
    echo "[$base] -> $csv"
    if "$BIN" "$wav" "$csv" "${EXTRA_ARGS[@]}"; then
        ok=$((ok + 1))
    else
        echo "  failed: $base" >&2
        fail=$((fail + 1))
    fi
done

echo "done: $ok ok, $fail failed (total ${#wavs[@]})"
[[ $fail -eq 0 ]]

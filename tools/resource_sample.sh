#!/usr/bin/env bash
# =============================================================================
#  resource_sample.sh — 앱 메모리(PSS) + Pi 발열/스로틀을 한 번에 외부 샘플링 (설치 불필요)
#
#  메모리(PSS)와 Pi 발열/스로틀을 한 루프로 함께 잰다. 자원(CPU/메모리/발열)을 앱 밖에서
#  재서 관측자 효과를 피한다. 하나의 CSV라 메모리·온도 시각이 자동으로 맞는다.
#
#  메모리(PSS)는 /proc/<pid>/smaps_rollup, 발열은 vcgencmd 에서 읽는다.
#  PSS 를 보는 이유: RSS 는 공유 라이브러리(Qt)를 통째로 세어 부풀려지고, PSS 는 공유 페이지를
#  공유 프로세스 수로 나눠 배분 → 앱이 '진짜' 차지하는 몫. 누수는 Private_Dirty 가 가장 깨끗.
#
#  사용:
#    tools/resource_sample.sh                 # TimeGrapher 자동 탐색, 1초 주기 → resource_ext.csv
#    tools/resource_sample.sh -i 2 -o run1.csv
#    tools/resource_sample.sh -p <PID>
#
#  결과 CSV:
#    epoch_s,pss_kb,pss_mb,private_dirty_kb,rss_kb,temp_c,arm_mhz,throttled_hex,now_throttling,ever_throttled
#  분석:  tools/perf_join.py ... --resource resource_ext.csv   (시간축 정렬)
# =============================================================================
set -euo pipefail

NAME="TimeGrapher"
PID=""
INTERVAL=1
OUT="resource_ext.csv"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--pid)      PID="$2"; shift 2 ;;
    -n|--name)     NAME="$2"; shift 2 ;;
    -i|--interval) INTERVAL="$2"; shift 2 ;;
    -o|--out)      OUT="$2"; shift 2 ;;
    -h|--help)     grep '^#' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "알 수 없는 옵션: $1 (도움말: -h)"; exit 1 ;;
  esac
done

command -v vcgencmd >/dev/null 2>&1 || { echo "오류: vcgencmd 가 없습니다 (라즈베리파이 전용)."; exit 1; }

# PID 미지정이면 이름으로 탐색
if [[ -z "$PID" ]]; then
  PID="$(pidof "$NAME" | awk '{print $1}')" || true
  [[ -z "$PID" ]] && { echo "오류: '$NAME' 프로세스를 찾을 수 없습니다. 앱을 먼저 실행하세요 (또는 -p PID)."; exit 1; }
fi
ROLLUP="/proc/$PID/smaps_rollup"
[[ -r "$ROLLUP" ]] || { echo "오류: $ROLLUP 를 읽을 수 없습니다 (PID/권한/커널 확인)."; exit 1; }

echo "==> 대상  : $NAME (PID $PID)"
echo "==> 지표  : PSS·Private_Dirty·RSS(메모리) + 온도·클럭·스로틀(발열)"
echo "==> 주기  : ${INTERVAL}s   출력: $OUT   (Ctrl+C 로 종료)"
echo "epoch_s,pss_kb,pss_mb,private_dirty_kb,rss_kb,temp_c,arm_mhz,throttled_hex,now_throttling,ever_throttled" > "$OUT"

START_PSS=""; MAX_T=0
while [[ -r "$ROLLUP" ]]; do
  now="$(date +%s.%N)"
  # ── 메모리 (smaps_rollup, kB) ──
  read -r pss_kb pdirty_kb rss_kb < <(awk '
    /^Pss:/{pss=$2} /^Private_Dirty:/{pd=$2} /^Rss:/{rss=$2} END{print pss, pd, rss}' "$ROLLUP")
  pss_mb="$(awk -v k="$pss_kb" 'BEGIN{printf "%.1f", k/1024}')"
  # ── 발열 (vcgencmd) ──
  temp="$(vcgencmd measure_temp | sed -E "s/temp=([0-9.]+).*/\1/")"
  arm="$(( $(vcgencmd measure_clock arm | cut -d= -f2) / 1000000 ))"
  thr_hex="$(vcgencmd get_throttled | cut -d= -f2)"
  thr_dec=$(( thr_hex ))
  # 현재 '제한 중' = 하위 4비트(bit0 저전압·bit1 주파수제한·bit2 스로틀·bit3 연온도제한) 중 하나라도 ON.
  #  ★ 발열 스로틀은 bit2가 아니라 bit1/bit3 로 먼저 걸린다 → 하위 4비트 전체를 봐야 한다.
  now_t=$(( (thr_dec & 0xF) != 0 ? 1 : 0 ))
  # 이력 = 상위 4비트(bit16~19) 중 하나라도 ON (부팅 후 한 번이라도 발생). 재부팅 시 초기화.
  ever_t=$(( (thr_dec & 0xF0000) != 0 ? 1 : 0 ))

  echo "$now,$pss_kb,$pss_mb,$pdirty_kb,$rss_kb,$temp,$arm,$thr_hex,$now_t,$ever_t" >> "$OUT"

  [[ -z "$START_PSS" ]] && START_PSS="$pss_kb"
  d_mb="$(awk -v a="$pss_kb" -v b="$START_PSS" 'BEGIN{printf "%+.1f", (a-b)/1024}')"
  ti=${temp%.*}; [[ "$ti" -gt "$MAX_T" ]] && MAX_T="$ti"
  flag=""; [[ "$now_t" == "1" ]] && flag=" ⚠️스로틀중"
  printf '\r PSS=%s MB(%s)  온도=%s°C(최고%s)%s   ' "$pss_mb" "$d_mb" "$temp" "$MAX_T" "$flag"
  sleep "$INTERVAL"
done
echo ""
echo "==> 프로세스 종료됨. 결과: $OUT"

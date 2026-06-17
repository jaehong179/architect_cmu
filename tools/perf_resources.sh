#!/bin/bash
# =============================================================================
#  perf_resources.sh — TimeGrapher 외부 자원 측정 (Raspberry Pi / Linux)
# -----------------------------------------------------------------------------
#  OS 자원(CPU% · PSS 메모리 · SoC 온도 · 서멀 스로틀)을 1Hz 로 CSV 에 남긴다.
#  앱 내부가 아니라 '밖'에서 측정 → 관측자 효과 0, 그리고 자원은 외부 도구로 통합.
#
#  사용:  perf_resources.sh <PID>
#    · 앱이 PERF_ENABLE=1 로 빌드되고 이 스크립트가 실행파일 옆에 있으면 앱이 자동 실행한다.
#    · <PID> 프로세스가 사라지면 스크립트도 자동 종료(고아 프로세스 방지).
#
#  출력:  이 스크립트와 같은 디렉터리의 perf_resources.csv
#    컬럼: epoch_ms,cpu_percent,pss_kb,soc_temp_c,throttled
#    · 앱의 perf_log.csv 헤더에 있는 epoch_ms_t0 와 이 파일의 epoch_ms 로 시간 정렬한다
#      (perf_log 의 절대시각 = epoch_ms_t0 + t_ms).
#    · 값이 N/A 인 항목(예 Windows·vcgencmd 없음)은 빈 칸으로 남긴다.
# =============================================================================
set -u
PID="${1:-}"
if [ -z "$PID" ]; then echo "usage: $0 <pid>" >&2; exit 1; fi

DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="$DIR/perf_resources.csv"
CLK="$(getconf CLK_TCK 2>/dev/null || echo 100)"
NCPU="$(nproc 2>/dev/null || echo 1)"

echo "epoch_ms,cpu_percent,pss_kb,soc_temp_c,throttled" > "$OUT"

prev_cpu=-1
prev_ms=0

read_pss() {        # PSS(kB): 공유 페이지를 비례 배분한 실점유(RSS 보다 정확). 커널 smaps_rollup 사용.
    if [ -r "/proc/$PID/smaps_rollup" ]; then
        awk '/^Pss:/{print $2; exit}' "/proc/$PID/smaps_rollup" 2>/dev/null
    elif [ -r "/proc/$PID/smaps" ]; then
        awk '/^Pss:/{s+=$2} END{print s}' "/proc/$PID/smaps" 2>/dev/null
    fi
}
read_temp() {       # SoC 온도(°C). millidegree → degree.
    if [ -r /sys/class/thermal/thermal_zone0/temp ]; then
        awk '{printf "%.1f", $1/1000.0}' /sys/class/thermal/thermal_zone0/temp 2>/dev/null
    fi
}
read_throttled() {  # 서멀/전압 스로틀 비트마스크(vcgencmd). 없으면 빈 칸.
    if command -v vcgencmd >/dev/null 2>&1; then
        vcgencmd get_throttled 2>/dev/null | sed -n 's/.*throttled=//p'
    fi
}

while kill -0 "$PID" 2>/dev/null; do
    now_ms="$(date +%s%3N)"

    # CPU 누적 시간(ticks) = utime(field14)+stime(field15). comm 괄호 뒤부터 파싱(공백/괄호 안전).
    cpu_pct=""
    stat_line="$(cat "/proc/$PID/stat" 2>/dev/null)" || break
    after="${stat_line##*) }"     # ')' 뒤 = state(field3) 부터
    # shellcheck disable=SC2086
    set -- $after                  # $1=state(3) ... utime=field14→$12, stime=field15→$13
    cur_cpu=$(( ${12:-0} + ${13:-0} ))
    if [ "$prev_cpu" -ge 0 ] && [ "$now_ms" -gt "$prev_ms" ]; then
        cpu_pct="$(awk -v dc=$((cur_cpu-prev_cpu)) -v dm=$((now_ms-prev_ms)) -v clk="$CLK" -v n="$NCPU" \
                   'BEGIN{ s=dm/1000.0; if(s>0){ v=(dc/clk)/s*100.0/n; if(v<0)v=0; printf "%.1f", v } }')"
    fi
    prev_cpu=$cur_cpu
    prev_ms=$now_ms

    pss="$(read_pss)"
    temp="$(read_temp)"
    thr="$(read_throttled)"

    echo "${now_ms},${cpu_pct},${pss},${temp},${thr}" >> "$OUT"
    sleep 1
done

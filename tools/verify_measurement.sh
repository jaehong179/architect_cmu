#!/usr/bin/env bash
# =============================================================================
#  verify_measurement.sh — 측정 산출물이 '제대로 기록됐는지' 정합성 점검 (QA 합격판정 아님)
#
#  확인 항목:
#    1) perf_log.csv  : 정렬 앵커(epoch_ms_t0) 존재 · 자원지표(cpu/rss/throttle) 부재 · 내부지표 기록됨
#    2) resource_ext  : PSS·온도 값이 합리적 범위로 채워짐 · now_throttling 이 throttled_hex 와 일치(수정 검증)
#    3) 시간정렬      : perf_join.py 가 두 로그를 합쳐 출력하는지
#  메모리는 '증가량 평가 없이' 기록 여부(min/max)만 본다.
#
#  사용: tools/verify_measurement.sh [perf_log.csv] [resource_ext.csv]
# =============================================================================
set -uo pipefail
cd "$(dirname "$0")/.."

PERF="${1:-build/perf_log.csv}"
RES="${2:-resource_ext.csv}"
ok=0; bad=0
pass(){ echo "  ✅ $*"; ok=$((ok+1)); }
fail(){ echo "  ❌ $*"; bad=$((bad+1)); }

echo "── 1. 내부 로그 ($PERF) ─────────────────────────────"
if [[ ! -s "$PERF" ]]; then fail "파일 없음/빈 파일"; else
  grep -q "epoch_ms_t0=" "$PERF" && pass "정렬 앵커 epoch_ms_t0 있음" || fail "앵커 없음 (구버전 빌드?)"
  if grep -qE ",(cpu_percent|rss_bytes|throttled_flag)," "$PERF"; then
    fail "내부에 자원지표가 남아있음 (외부로 빠졌어야 함)"
  else pass "자원지표(cpu/rss/throttle) 내부에 없음 (정상)"; fi
  nrows=$(tail -n +4 "$PERF" | grep -c ,)
  [[ "$nrows" -gt 0 ]] && pass "내부 지표 $nrows 행 기록됨" || fail "내부 지표 0행"
  # NaN/inf 오염 점검
  if tail -n +4 "$PERF" | cut -d, -f5 | grep -qiE "nan|inf"; then fail "값에 NaN/inf 있음"; else pass "값에 NaN/inf 없음"; fi
  echo "  · 기록된 지표:"; tail -n +4 "$PERF" | cut -d, -f4 | sort | uniq -c | sort -rn | awk '{printf "      %5d  %s\n",$1,$2}'
fi

echo ""
echo "── 2. 외부 자원 로그 ($RES) ─────────────────────────"
if [[ ! -s "$RES" ]]; then fail "파일 없음/빈 파일"; else
python3 - "$RES" <<'PY'
import csv,sys
ok=bad=0
def p(m):
    global ok; ok+=1; print(f"  ✅ {m}")
def f(m):
    global bad; bad+=1; print(f"  ❌ {m}")
rows=list(csv.DictReader(open(sys.argv[1])))
need={'pss_mb','temp_c','arm_mhz','throttled_hex','now_throttling'}
if not rows: f("표본 0개")
elif not need<=set(rows[0]): f(f"컬럼 누락: {need-set(rows[0])}")
else:
    n=len(rows)
    pss=[float(r['pss_mb']) for r in rows]
    tmp=[float(r['temp_c']) for r in rows]
    # PSS 기록 정합성 (증가량 평가 아님 — 값이 채워지고 합리적인지만)
    if all(x>0 for x in pss) and max(pss)<8000: p(f"PSS 기록됨 (min={min(pss):.1f} max={max(pss):.1f} MB) — 증가량은 평가 안 함")
    else: f(f"PSS 값 이상 (min={min(pss)} max={max(pss)})")
    # 온도 합리적 범위
    if all(20<=x<=110 for x in tmp): p(f"온도 기록됨, 합리적 범위 (min={min(tmp):.1f} max={max(tmp):.1f}°C)")
    else: f(f"온도 범위 이상 (min={min(tmp)} max={max(tmp)})")
    # ★ now_throttling 이 throttled_hex 하위4비트와 일치하는지 (이번에 고친 부분 검증)
    mism=sum(1 for r in rows if (int(r['throttled_hex'],16)&0xF and r['now_throttling']!='1') or (not int(r['throttled_hex'],16)&0xF and r['now_throttling']!='0'))
    if mism==0: p("now_throttling 이 throttled_hex 와 100% 일치 (비트수정 정상)")
    else: f(f"now_throttling 불일치 {mism}/{n} (샘플러 비트로직 확인 필요)")
    thr=sum(1 for r in rows if int(r['throttled_hex'],16)&0xF)
    print(f"  · 표본 {n}개, 측정중 스로틀 {thr}개 ({thr/n*100:.0f}%) — 참고용(합격판정 아님)")
sys.exit(1 if bad else 0)
PY
  [[ $? -eq 0 ]] || bad=$((bad+1))
fi

echo ""
echo "── 3. 시간정렬 (perf_join.py) ───────────────────────"
if tools/perf_join.py "$PERF" --resource "$RES" -o /tmp/_join_check.csv 2>/dev/null; then
  jl=$(tail -n +2 /tmp/_join_check.csv | wc -l)
  [[ "$jl" -gt 0 ]] && pass "정렬 통합 $jl 행 생성 (perf+resource 공통축)" || fail "통합 결과 0행"
  rm -f /tmp/_join_check.csv
else fail "perf_join 실패 (앵커/형식 확인)"; fi

echo ""
echo "════════════════════════════════════════════════════"
if [[ "$bad" -eq 0 ]]; then echo " → 데이터가 제대로 기록됨 (정합성 OK)"
else echo " → ❌ 문제 $bad 건 — 위 항목 확인 필요"; fi

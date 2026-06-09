#!/usr/bin/env python3
# =============================================================================
#  analyze_perf.py — 측정 결과 통합 분석 리포트 (perf_log.csv + 선택적 resource_ext.csv)
#
#  하던 즉석 명령(stat awk·클럭버킷 python)을 한 번에: 지표별 통계(n·평균·중앙·p95·p99·최악) +
#  알려진 QA 목표 대비 합격/미달 + 외부 자원 요약 + 발열 영향(클럭버킷) 까지 출력.
#
#  사용:
#    tools/analyze_perf.py                                   # build/perf_log.csv 만
#    tools/analyze_perf.py build/perf_log.csv --resource resource_ext.csv
#    tools/analyze_perf.py --resource resource_ext.csv --bucket disp_paint_ms
#
#  의존성: 파이썬 표준 라이브러리만.  ※ 합격/미달은 '참고'이며 최종 판정은 가이드 기준으로.
# =============================================================================
import argparse, csv, sys

# 워밍업 제외: 각 지표의 첫 N샘플을 무조건 버린다(시작 직후 미수렴 이상치 제거).
#  예) rate_err 는 비트가 쌓이기 전 첫 2샘플이 -3.5 로 튀므로, 통계·판정에서 제외해야 정확하다.
WARMUP_SKIP = 2

# 지표별 목표 (docs/*/PERF_VERIFICATION_GUIDE.md). kind=latency(양수,작을수록 좋음)/error(부호有,절대값 한계)
TARGETS = {
    'e2e_full_ms':      {'kind':'latency','mean':50,'worst':100,'unit':'ms', 'label':'종단지연 (A-1/QA-LT-01)'},
    'ui_loop_lag_ms':   {'kind':'latency','worst':200,           'unit':'ms', 'label':'UI 응답 (A-3/QA-RT-01)'},
    'onset_err_ms':     {'kind':'latency','worst':0.5,           'unit':'ms', 'label':'Onset 오차 (E/QA-AC-02)'},
    'peak_err_ms':      {'kind':'latency','worst':0.2,           'unit':'ms', 'label':'Peak 오차 (E/QA-AC-02)'},
    'rate_err_s_per_d': {'kind':'error',  'bound':1,             'unit':'s/d','label':'정확도 Rate (G-1/QA-CO-01)'},
    'beaterr_err_ms':   {'kind':'error',  'bound':0.1,           'unit':'ms', 'label':'정확도 BeatError (G-1)'},
    'amp_err_deg':      {'kind':'error',  'bound':5,             'unit':'deg','label':'정확도 Amplitude (G-1)'},
}

def pct(sorted_vals, p):
    if not sorted_vals: return float('nan')
    i = min(len(sorted_vals)-1, int(len(sorted_vals)*p))
    return sorted_vals[i]

def read_perf(path):
    epoch_ms_t0=None; header=None; rows=[]
    for line in open(path):
        line=line.rstrip('\n')
        if line.startswith('#'):
            for tok in line.split():
                if tok.startswith('epoch_ms_t0='):
                    try: epoch_ms_t0=int(tok.split('=',1)[1])
                    except ValueError: pass
            continue
        if header is None: header=line.split(','); continue
        p=line.split(',')
        if len(p)<len(header): continue
        d=dict(zip(header,p))
        try: d['t_ms']=float(d['t_ms']); d['value']=float(d['value'])
        except ValueError: continue
        rows.append(d)
    return epoch_ms_t0, rows

def main():
    ap=argparse.ArgumentParser(description='측정 결과 통합 분석 리포트')
    ap.add_argument('perf', nargs='?', default='build/perf_log.csv')
    ap.add_argument('--resource', help='resource_ext.csv (발열/메모리 요약 + 클럭버킷)')
    ap.add_argument('--bucket', help='클럭대별로 쪼개 볼 지연 지표 (기본: e2e_full_ms 있으면 그것, 없으면 disp_paint_ms)')
    args=ap.parse_args()

    epoch_ms_t0, rows = read_perf(args.perf)
    if not rows: sys.exit(f"오류: {args.perf} 에 데이터 행이 없습니다.")

    # 지표별 수집 (시간순 (t_ms, value)) → 워밍업 첫 WARMUP_SKIP 샘플 무조건 제외
    raw={}
    for d in rows: raw.setdefault(d['metric'],[]).append((d['t_ms'], d['value']))
    by={}; skipped=0
    for m,seq in raw.items():
        if len(seq)>WARMUP_SKIP: by[m]=seq[WARMUP_SKIP:]; skipped+=WARMUP_SKIP
        else:                    by[m]=seq[:]               # 표본이 너무 적으면 그대로(통계 의미 약함)
    vals=lambda m:[v for _,v in by[m]]

    print(f"━━ 측정 분석: {args.perf}  (앵커 epoch_ms_t0={'있음' if epoch_ms_t0 else '없음'}) ━━")
    print(f"   총 {len(rows)}행 / 지표 {len(raw)}종   ·   워밍업 제외: 지표별 첫 {WARMUP_SKIP}샘플(총 {skipped}행)\n")

    print("【 통계 (단위는 지표별 · 워밍업 제외 후) 】")
    print(f"  {'지표':<22}{'n':>6}{'평균':>10}{'중앙':>10}{'p95':>10}{'p99':>10}{'최악':>10}")
    for m in sorted(by, key=lambda k:-len(by[k])):
        v=sorted(vals(m)); n=len(v); mean=sum(v)/n
        worst = max(v, key=abs)   # error 지표는 절대값 최대
        print(f"  {m:<22}{n:>6}{mean:>10.3f}{v[n//2]:>10.3f}{pct(v,0.95):>10.3f}{pct(v,0.99):>10.3f}{worst:>10.3f}")

    print("\n【 QA 목표 대비 (참고 — 최종 판정은 가이드 기준) 】")
    any_t=False
    for m,t in TARGETS.items():
        if m not in by: continue
        any_t=True; v=sorted(vals(m)); n=len(v); mean=sum(v)/n
        if t['kind']=='latency':
            worst=max(v); checks=[]
            if 'mean' in t:  checks.append(('평균', mean, t['mean'], mean<=t['mean']))
            if 'worst' in t: checks.append(('최악', worst, t['worst'], worst<=t['worst']))
        else:  # error: 절대값 한계
            worst=max(abs(x) for x in v)
            checks=[('|최악|', worst, t['bound'], worst<=t['bound'])]
        verdict='✅' if all(c[3] for c in checks) else '❌'
        detail=' · '.join(f"{lbl}={val:.3f}{'≤' if ok else '>'}{tgt}{t['unit']}" for lbl,val,tgt,ok in checks)
        print(f"  {verdict} {t['label']:<28} {detail}")
    # 검출률
    if 'a_match' in by and 'gt_total' in by:
        gt=vals('gt_total')[-1]; det=len(by['a_match'])
        if gt>0: print(f"  · 검출률 = {det}/{int(gt)} = {det/gt*100:.1f}% (목표 ≥95%)")
    if not any_t: print("  (목표가 정의된 지표가 이번 로그에 없음)")

    # ── 외부 자원 (Pi: pss/temp/throttle  ·  Windows: working_set/private/cpu — 있는 컬럼만) ──
    if args.resource:
        rr=list(csv.DictReader(open(args.resource)))
        def col(name):  # 해당 컬럼의 float 리스트(비어있으면 빈 리스트)
            return [float(r[name]) for r in rr if r.get(name) not in (None,"")]
        if rr:
            print(f"\n【 외부 자원 ({args.resource}, {len(rr)}표본) 】")
            # 메모리: PSS(Pi) 우선, 없으면 Working Set/Private(Windows)
            if col('pss_mb'):
                m=col('pss_mb'); print(f"  메모리 PSS        : {m[0]:.1f} → {m[-1]:.1f} MB  (min {min(m):.1f} / max {max(m):.1f})")
            if col('working_set_mb'):
                m=col('working_set_mb'); print(f"  메모리 WorkingSet : {m[0]:.1f} → {m[-1]:.1f} MB  (min {min(m):.1f} / max {max(m):.1f})")
            if col('private_mb'):
                m=col('private_mb'); print(f"  메모리 Private    : {m[0]:.1f} → {m[-1]:.1f} MB  (누수 분석용, Windows)")
            if col('cpu_percent'):
                c=col('cpu_percent'); print(f"  CPU              : 평균 {sum(c)/len(c):.1f}%  최고 {max(c):.1f}% (목표 ≤70%)")
            # 발열/스로틀: Pi 전용 — temp_c/arm_mhz/throttled_hex 있을 때만
            tmp=col('temp_c'); clk=[int(x) for x in col('arm_mhz')]
            has_thr=any(r.get('throttled_hex') for r in rr)
            if tmp and clk:
                thr=sum(1 for r in rr if r.get('throttled_hex') and int(r['throttled_hex'],16)&0xF) if has_thr else 0
                print(f"  발열             : 최고 {max(tmp):.1f}°C   클럭 {min(clk)}~{max(clk)}MHz")
                if has_thr:
                    print(f"  스로틀           : 측정중 {thr}/{len(rr)} ({thr/len(rr)*100:.0f}%)" +
                          ("  ⚠️ 측정이 발열에 영향받음" if thr else ""))
            else:
                print("  발열/스로틀      : (해당 없음 — Windows 또는 Pi 아님)")

            # 클럭버킷 상관 — 지연이 CPU클럭 바운드인지 (arm_mhz 있을 때만 = Pi)
            bm = args.bucket or ('e2e_full_ms' if 'e2e_full_ms' in by else 'disp_paint_ms')
            if bm in by and epoch_ms_t0 and clk:
                rms=[(float(r['epoch_s'])*1000, int(r['arm_mhz'])) for r in rr]
                rms.sort()
                import bisect
                rk=[x[0] for x in rms]
                buckets={}
                for tms,val in by[bm]:
                    ev=epoch_ms_t0+tms; i=bisect.bisect_left(rk,ev)
                    best=None
                    for j in (i-1,i):
                        if 0<=j<len(rk) and (best is None or abs(rk[j]-ev)<abs(rk[best]-ev)): best=j
                    if best is None or abs(rk[best]-ev)>1500: continue
                    mhz=rms[best][1]
                    k='<2000MHz(스로틀)' if mhz<2000 else '2000-2349' if mhz<2350 else '2400MHz(정상)'
                    buckets.setdefault(k,[]).append(val)
                if buckets:
                    print(f"\n【 발열 영향: '{bm}' 클럭대별 평균 (CPU클럭 바운드 여부) 】")
                    for k in ['2400MHz(정상)','2000-2349','<2000MHz(스로틀)']:
                        if k in buckets:
                            b=buckets[k]; print(f"  {k:18s}: n={len(b):4d}  평균={sum(b)/len(b):.2f}")
                    print("  → 클럭대별로 값이 거의 같으면 CPU클럭 바운드 아님(GPU/디스플레이/큐렌더 바운드).")

if __name__=='__main__':
    main()

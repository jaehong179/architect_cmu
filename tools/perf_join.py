#!/usr/bin/env python3
# =============================================================================
#  perf_join.py — 내부 계측(perf_log.csv) + 외부 자원 로그(mem/thermal)를 시간축으로 정렬
#
#  ★ 시간 정렬 원리
#    내부 perf_log.csv 의 t_ms 는 단조시계(앱 시작=0). 절대시각이 아니다.
#    외부 mem_ext.csv / thermal_ext.csv 의 epoch_s 는 Unix epoch(벽시계).
#    perf_log.csv 헤더의  epoch_ms_t0  = t_ms=0 순간의 벽시계 epoch(ms) 이므로:
#        event_epoch_ms = epoch_ms_t0 + t_ms          (내부 → 벽시계)
#        ext_t_ms       = epoch_s*1000 - epoch_ms_t0  (외부 → 내부축)
#    이 한 줄로 두 축을 양방향 변환한다.
#
#  사용:
#    # 1) 통합 롱포맷 CSV (epoch_ms, t_ms, source, metric, value) 로 합치기
#    tools/perf_join.py build/perf_log.csv --mem mem_ext.csv --thermal thermal_ext.csv -o joined.csv
#
#    # 2) 내부 지표 이벤트마다 '그 시각의' 온도/메모리를 최근접으로 붙이기 (상관 분석)
#    tools/perf_join.py build/perf_log.csv --mem mem_ext.csv --thermal thermal_ext.csv \
#        --correlate e2e_full_ms --tolerance 1500 -o corr.csv
#
#  의존성: 파이썬 표준 라이브러리만 (pandas 불필요)
# =============================================================================
import argparse, csv, sys, bisect

def read_perf(path):
    """perf_log.csv → (epoch_ms_t0, [rows]). rows: dict(t_ms, section, qa, metric, value, unit, extra)."""
    epoch_ms_t0 = None
    rows, header = [], None
    with open(path, newline="") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("#"):
                # "# ... epoch_ms_t0=1780958947123 ..." 에서 추출
                for tok in line.split():
                    if tok.startswith("epoch_ms_t0="):
                        try: epoch_ms_t0 = int(tok.split("=", 1)[1])
                        except ValueError: pass
                continue
            if header is None:
                header = line.split(",")          # t_ms,section,qa,metric,value,unit,extra
                continue
            parts = line.split(",")
            if len(parts) < len(header): continue
            d = dict(zip(header, parts))
            try:
                d["t_ms"] = float(d["t_ms"]); d["value"] = float(d["value"])
            except ValueError:
                continue
            rows.append(d)
    if epoch_ms_t0 is None:
        sys.exit("오류: perf_log.csv 헤더에서 epoch_ms_t0 를 찾지 못했습니다.\n"
                 "      → 앵커 추가 후 다시 측정한 CSV 인지 확인하세요 (구버전 CSV는 정렬 불가).")
    return epoch_ms_t0, rows

def read_external(path, value_cols=None):
    """epoch_s 를 첫 컬럼으로 갖는 외부 CSV → [(epoch_ms, {col: float})].
       value_cols=None 이면 epoch_s 외 모든 숫자 컬럼을 자동 인식(플랫폼 무관: Pi/Windows 합본)."""
    out = []
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        cols = value_cols if value_cols is not None else [c for c in (r.fieldnames or []) if c != "epoch_s"]
        for row in r:
            try: epoch_ms = float(row["epoch_s"]) * 1000.0
            except (KeyError, ValueError, TypeError): continue
            vals = {}
            for c in cols:
                if c in row and row[c] not in (None, ""):
                    try: vals[c] = float(row[c])
                    except ValueError: pass
            out.append((epoch_ms, vals))
    out.sort(key=lambda x: x[0])
    return out

def header_cols(path):
    """자원 CSV 헤더에서 epoch_s 를 뺀 컬럼 목록(출력 순서 유지)."""
    with open(path, newline="") as f:
        return [c for c in (next(csv.reader(f), []) ) if c != "epoch_s"]

def nearest(series_ms, series_vals, target_ms, tol_ms):
    """정렬된 epoch_ms 목록에서 target 에 가장 가까운 표본을 tol 이내로 반환(없으면 None)."""
    if not series_ms: return None
    i = bisect.bisect_left(series_ms, target_ms)
    best = None
    for j in (i-1, i):
        if 0 <= j < len(series_ms):
            dt = abs(series_ms[j] - target_ms)
            if dt <= tol_ms and (best is None or dt < best[0]):
                best = (dt, series_vals[j])
    return best[1] if best else None

MEM_COLS     = ["pss_mb", "private_dirty_kb", "rss_kb"]
THERMAL_COLS = ["temp_c", "arm_mhz", "now_throttling", "ever_throttled"]

def main():
    ap = argparse.ArgumentParser(description="perf_log.csv + 외부 자원 로그 시간 정렬")
    ap.add_argument("perf", help="내부 perf_log.csv 경로")
    ap.add_argument("--mem", help="mem_ext.csv 경로")
    ap.add_argument("--thermal", help="thermal_ext.csv 경로")
    ap.add_argument("--resource", help="resource_ext.csv 경로 (메모리+발열 합본 — --mem/--thermal 대신)")
    ap.add_argument("--correlate", metavar="METRIC",
                    help="이 내부 지표 이벤트마다 최근접 온도/메모리를 붙임 (예: e2e_full_ms)")
    ap.add_argument("--tolerance", type=float, default=1500.0,
                    help="--correlate 시 최근접 허용 시간차(ms). 기본 1500")
    ap.add_argument("-o", "--out", default="-", help="출력 CSV (기본: 표준출력)")
    args = ap.parse_args()

    epoch_ms_t0, perf_rows = read_perf(args.perf)
    # --resource(합본)는 한 파일에서 모든 컬럼을 자동 인식 → Pi(pss/temp/throttle)·Windows(cpu/ws/private) 둘 다 동작.
    if args.resource:
        mem = read_external(args.resource); thr = []          # 모든 컬럼을 mem 한 소스로
        out_cols = header_cols(args.resource)
        src_label = "resource"
    else:
        mem = read_external(args.mem, MEM_COLS) if args.mem else []
        thr = read_external(args.thermal, THERMAL_COLS) if args.thermal else []
        out_cols = (MEM_COLS if args.mem else []) + (THERMAL_COLS if args.thermal else [])
        src_label = "mem"
    mem_ms,  mem_v  = [m[0] for m in mem], [m[1] for m in mem]
    thr_ms,  thr_v  = [t[0] for t in thr], [t[1] for t in thr]

    out = sys.stdout if args.out == "-" else open(args.out, "w", newline="")
    w = csv.writer(out)

    if args.correlate:
        # 내부 지정 지표 이벤트별로 그 시각의 외부 값을 최근접으로 첨부
        cols = ["t_ms", "epoch_ms", "metric", "value"] + out_cols + ["matched"]
        w.writerow(cols)
        n_hit = 0
        for d in perf_rows:
            if d["metric"] != args.correlate: continue
            ev_epoch = epoch_ms_t0 + d["t_ms"]
            mv = nearest(mem_ms, mem_v, ev_epoch, args.tolerance) or {}
            tv = nearest(thr_ms, thr_v, ev_epoch, args.tolerance) or {}
            both = {**mv, **tv}
            matched = "yes" if both else "no"
            n_hit += matched == "yes"
            row = [f'{d["t_ms"]:.3f}', f'{ev_epoch:.0f}', d["metric"], f'{d["value"]:.4f}']
            row += [f'{both[c]:.4f}' if c in both else "" for c in out_cols]
            row += [matched]
            w.writerow(row)
        sys.stderr.write(f"[perf_join] '{args.correlate}' 이벤트에 외부값 매칭: {n_hit} 건 (tol={args.tolerance:.0f}ms)\n")
    else:
        # 통합 롱포맷: 모든 소스를 epoch_ms / t_ms 공통축으로 합쳐 출력
        w.writerow(["epoch_ms", "t_ms", "source", "metric", "value"])
        recs = []
        for d in perf_rows:
            ev = epoch_ms_t0 + d["t_ms"]
            recs.append((ev, d["t_ms"], "perf", d["metric"], d["value"]))
        for em, vals in mem:
            for c, v in vals.items():
                recs.append((em, em - epoch_ms_t0, src_label, c, v))
        for em, vals in thr:
            for c, v in vals.items():
                recs.append((em, em - epoch_ms_t0, "thermal", c, v))
        recs.sort(key=lambda r: r[0])
        for ev, tms, src, metric, val in recs:
            w.writerow([f"{ev:.0f}", f"{tms:.3f}", src, metric, f"{val:.4f}"])
        sys.stderr.write(f"[perf_join] 통합 {len(recs)} 행 (perf+mem+thermal) → epoch_ms/t_ms 공통축\n")

    if out is not sys.stdout: out.close()

if __name__ == "__main__":
    main()

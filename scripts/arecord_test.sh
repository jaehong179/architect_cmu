#!/bin/bash
# ============================================================
# arecord 192kHz mic input + 60s CPU usage monitor
# + Audio sample drop detection (triple xrun/overrun check)
# ============================================================

DURATION=60
SAMPLE_RATE=192000
CHANNELS=1
FORMAT=S16_LE
BITS=16                        # S16_LE = 16-bit
OUTPUT_FILE="recorded_$(date +%Y%m%d_%H%M%S).wav"
CPU_LOG="cpu_usage_$(date +%Y%m%d_%H%M%S).log"
XRUN_LOG="xrun_$(date +%Y%m%d_%H%M%S).log"
ARECORD_STDERR=$(mktemp /tmp/arecord_stderr.XXXXXX)

# ── Calculate expected WAV file size ─────────────────────────
BYTES_PER_SAMPLE=$(( BITS / 8 ))
BYTES_PER_SEC=$(( SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE ))
EXPECTED_DATA_BYTES=$(( BYTES_PER_SEC * DURATION ))
WAV_HEADER_BYTES=44
EXPECTED_TOTAL_BYTES=$(( EXPECTED_DATA_BYTES + WAV_HEADER_BYTES ))

echo "============================================"
echo " arecord 192kHz + CPU + Drop Detection"
echo "============================================"
echo " Output file   : $OUTPUT_FILE"
echo " CPU log       : $CPU_LOG"
echo " Xrun log      : $XRUN_LOG"
echo " Duration      : ${DURATION}s"
echo " Expected size : $(numfmt --to=iec $EXPECTED_TOTAL_BYTES 2>/dev/null || echo "${EXPECTED_TOTAL_BYTES} bytes")"
echo "============================================"
echo ""

# ── Read initial ALSA xrun counter ───────────────────────────
get_alsa_xrun() {
    # Sum xrun counts from /proc/asound/card*/pcm*/capture*/status
    local total=0
    for f in /proc/asound/card*/pcm*/cap*/status; do
        [ -f "$f" ] || continue
        local v
        v=$(grep -i "xrun" "$f" 2>/dev/null | grep -o '[0-9]*' | head -1)
        total=$(( total + ${v:-0} ))
    done
    echo "$total"
}

ALSA_XRUN_INIT=$(get_alsa_xrun)

# ── Write xrun log header ─────────────────────────────────────
echo "Timestamp,Elapsed(s),Overrun_Count_stderr,ALSA_Xrun_Delta,File_Size_Bytes,Expected_Bytes,Drop_Detected" \
    > "$XRUN_LOG"

# ── Write CPU log header ──────────────────────────────────────
echo "Timestamp,CPU_Total(%),User(%),System(%),Idle(%),arecord_PID,arecord_CPU(%),arecord_MEM(%),Xrun_Total" \
    > "$CPU_LOG"

# ── Launch arecord in background (capture stderr to file) ─────
arecord \
    --rate=$SAMPLE_RATE \
    --channels=$CHANNELS \
    --format=$FORMAT \
    --duration=$DURATION \
    --verbose \
    "$OUTPUT_FILE" 2>"$ARECORD_STDERR" &

ARECORD_PID=$!
echo "[INFO] arecord started (PID: $ARECORD_PID)"
echo ""

sleep 0.5
if ! kill -0 "$ARECORD_PID" 2>/dev/null; then
    echo "[ERROR] arecord failed to start. Check available devices: arecord -l"
    cat "$ARECORD_STDERR"
    rm -f "$ARECORD_STDERR"
    exit 1
fi

# ── Print table header ────────────────────────────────────────
printf "%-8s | %-10s | %-7s | %-6s | %-7s | %-10s | %-10s | %-12s | %-10s\n" \
    "Time(s)" "TotalCPU%" "User%" "Sys%" "Idle%" "arecord%" "File(MB)" "Overruns" "ALSA_Xrun"
printf '%s\n' "---------|------------|---------|--------|---------|------------|------------|--------------|----------"

START_TIME=$(date +%s)
ELAPSED=0
TOTAL_XRUN=0
PREV_OVERRUN_COUNT=0
DROP_EVENTS=0

while [ "$ELAPSED" -lt "$DURATION" ]; do
    TIMESTAMP=$(date +"%H:%M:%S")

    # ── (1) Count overrun/xrun/drop messages from stderr ──────
    OVERRUN_COUNT=$(grep -ci "overrun\|xrun\|drop" "$ARECORD_STDERR" 2>/dev/null); OVERRUN_COUNT=$(( ${OVERRUN_COUNT:-0} + 0 ))
    NEW_OVERRUNS=$(( OVERRUN_COUNT - PREV_OVERRUN_COUNT ))
    PREV_OVERRUN_COUNT=$OVERRUN_COUNT

    # ── (2) Read ALSA kernel xrun counter delta ────────────────
    ALSA_XRUN_NOW=$(get_alsa_xrun)
    ALSA_XRUN_DELTA=$(( ALSA_XRUN_NOW - ALSA_XRUN_INIT ))

    # ── (3) Compare live file size vs expected size ────────────
    CURRENT_SIZE=0
    if [ -f "$OUTPUT_FILE" ]; then
        CURRENT_SIZE=$(stat -c%s "$OUTPUT_FILE" 2>/dev/null || echo 0)
    fi
    EXPECTED_NOW=$(( WAV_HEADER_BYTES + BYTES_PER_SEC * ELAPSED ))
    SIZE_DIFF=$(( EXPECTED_NOW - CURRENT_SIZE ))
    # Flag as drop if file lags more than 1 second of audio data
    if [ "$SIZE_DIFF" -gt "$BYTES_PER_SEC" ] && [ "$ELAPSED" -gt 2 ]; then
        SIZE_DROP=1
        DROP_EVENTS=$(( DROP_EVENTS + 1 ))
    else
        SIZE_DROP=0
    fi

    # Drop flag: set if any of the three checks triggered
    DROP_FLAG=0
    [ "$NEW_OVERRUNS" -gt 0 ]                    && DROP_FLAG=1
    [ "$ALSA_XRUN_DELTA" -gt "$TOTAL_XRUN" ]     && DROP_FLAG=1 && TOTAL_XRUN=$ALSA_XRUN_DELTA
    [ "$SIZE_DROP" -eq 1 ]                        && DROP_FLAG=1

    DROP_INDICATOR=""
    [ "$DROP_FLAG" -eq 1 ] && DROP_INDICATOR=" ⚠ DROP"

    # ── Collect CPU stats ─────────────────────────────────────
    CPU_STATS=$(top -bn2 -d0.5 | grep "Cpu(s)" | tail -1)
    CPU_USER=$(echo "$CPU_STATS"  | awk '{print $2}' | tr -d '%us,')
    CPU_SYS=$(echo "$CPU_STATS"   | awk '{print $4}' | tr -d '%sy,')
    CPU_IDLE=$(echo "$CPU_STATS"  | awk '{print $8}' | tr -d '%id,')
    CPU_TOTAL=$(echo "$CPU_USER $CPU_SYS" | awk '{printf "%.1f", $1 + $2}')

    if kill -0 "$ARECORD_PID" 2>/dev/null; then
        PROC_STATS=$(ps -p "$ARECORD_PID" -o pid,pcpu,pmem --no-headers 2>/dev/null)
        PROC_CPU=$(echo "$PROC_STATS" | awk '{print $2}')
        PROC_MEM=$(echo "$PROC_STATS" | awk '{print $3}')
        PROC_CPU=${PROC_CPU:-0.0}
        PROC_MEM=${PROC_MEM:-0.0}
    else
        PROC_CPU="N/A"; PROC_MEM="N/A"
    fi

    FILE_MB=$(echo "$CURRENT_SIZE" | awk '{printf "%.2f", $1/1048576}')

    # ── Print console row ─────────────────────────────────────
    printf "%-8d | %-10s | %-7s | %-6s | %-7s | %-10s | %-10s | %-12s | %-10s%s\n" \
        "$ELAPSED" "${CPU_TOTAL}%" "${CPU_USER}%" "${CPU_SYS}%" "${CPU_IDLE}%" \
        "${PROC_CPU}%" "${FILE_MB}MB" \
        "OVR:${OVERRUN_COUNT}" "Δ${ALSA_XRUN_DELTA}" \
        "$DROP_INDICATOR"

    # ── Write CSV log entries ─────────────────────────────────
    echo "$TIMESTAMP,$CPU_TOTAL,$CPU_USER,$CPU_SYS,$CPU_IDLE,$ARECORD_PID,$PROC_CPU,$PROC_MEM,$OVERRUN_COUNT" \
        >> "$CPU_LOG"
    echo "$TIMESTAMP,$ELAPSED,$OVERRUN_COUNT,$ALSA_XRUN_DELTA,$CURRENT_SIZE,$EXPECTED_NOW,$DROP_FLAG" \
        >> "$XRUN_LOG"

    sleep 1
    ELAPSED=$(( $(date +%s) - START_TIME ))
done

# Wait for arecord to finish
wait "$ARECORD_PID" 2>/dev/null
FINAL_SIZE=$(stat -c%s "$OUTPUT_FILE" 2>/dev/null || echo 0)

echo ""
echo "============================================"
echo " Measurement complete — Summary"
echo "============================================"

# ── CPU summary ───────────────────────────────────────────────
echo ""
echo "[CPU Usage Summary]"
awk -F',' 'NR>1 {
    sum_total += $2; sum_user += $3; sum_sys += $4; sum_idle += $5; sum_proc += $7; count++
}
END {
    if (count > 0) {
        printf "  Avg total CPU  : %.2f%%\n", sum_total / count
        printf "  Avg user       : %.2f%%\n", sum_user  / count
        printf "  Avg system     : %.2f%%\n", sum_sys   / count
        printf "  Avg idle       : %.2f%%\n", sum_idle  / count
        printf "  Avg arecord    : %.2f%%\n", sum_proc  / count
        printf "  Samples taken  : %d\n",     count
    }
}' "$CPU_LOG"

# ── Drop detection summary ────────────────────────────────────
echo ""
echo "[Audio Sample Drop Summary]"

# (1) Total stderr overrun messages
FINAL_OVERRUNS=$(grep -ci "overrun\|xrun\|drop" "$ARECORD_STDERR" 2>/dev/null); FINAL_OVERRUNS=$(( ${FINAL_OVERRUNS:-0} + 0 ))
echo "  (1) stderr overrun messages : ${FINAL_OVERRUNS}"

# (2) ALSA kernel xrun delta
ALSA_XRUN_FINAL=$(get_alsa_xrun)
ALSA_TOTAL_DELTA=$(( ALSA_XRUN_FINAL - ALSA_XRUN_INIT ))
echo "  (2) ALSA kernel xrun delta  : ${ALSA_TOTAL_DELTA}"

# (3) WAV file size validation
echo "  (3) WAV file size check"
echo "      Expected : ${EXPECTED_TOTAL_BYTES} bytes ($(numfmt --to=iec $EXPECTED_TOTAL_BYTES 2>/dev/null))"
echo "      Actual   : ${FINAL_SIZE} bytes ($(numfmt --to=iec $FINAL_SIZE 2>/dev/null))"
SIZE_SHORTFALL=$(( EXPECTED_TOTAL_BYTES - FINAL_SIZE ))
if [ "$SIZE_SHORTFALL" -gt 0 ]; then
    DROPPED_SAMPLES=$(( SIZE_SHORTFALL / BYTES_PER_SAMPLE / CHANNELS ))
    DROPPED_MS=$(echo "$DROPPED_SAMPLES $SAMPLE_RATE" | awk '{printf "%.2f", $1/$2*1000}')
    echo "      Shortfall: ${SIZE_SHORTFALL} bytes — est. ${DROPPED_SAMPLES} samples (${DROPPED_MS} ms) lost"
else
    echo "      Size OK (diff: ${SIZE_SHORTFALL} bytes)"
fi

# ── Final verdict ─────────────────────────────────────────────
echo ""
TOTAL_DROP_SCORE=$(( FINAL_OVERRUNS + ALSA_TOTAL_DELTA + DROP_EVENTS ))
if [ "$TOTAL_DROP_SCORE" -eq 0 ] && [ "$SIZE_SHORTFALL" -le 0 ]; then
    echo "  ✅ PASS: No drops detected — recording completed successfully."
else
    echo "  ❌ FAIL: Sample drops detected!"
    [ "$FINAL_OVERRUNS"   -gt 0 ] && echo "     - Overrun events  : ${FINAL_OVERRUNS}"
    [ "$ALSA_TOTAL_DELTA" -gt 0 ] && echo "     - ALSA xrun delta : ${ALSA_TOTAL_DELTA}"
    [ "$DROP_EVENTS"      -gt 0 ] && echo "     - File size drops  : ${DROP_EVENTS} detected"
    [ "$SIZE_SHORTFALL"   -gt 0 ] && echo "     - Estimated loss   : ${DROPPED_SAMPLES} samples / ${DROPPED_MS} ms"
fi

# Clean up temporary stderr file
rm -f "$ARECORD_STDERR"

echo ""
echo " Output file : $OUTPUT_FILE"
echo " CPU log     : $CPU_LOG"
echo " Xrun log    : $XRUN_LOG"
echo "============================================"

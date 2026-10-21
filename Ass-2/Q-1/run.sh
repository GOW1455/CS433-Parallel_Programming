#!/usr/bin/env bash
# Compile and benchmark q1 (P1-P3: lock-free sem, POSIX sem, cond-var).
# Sweeps several (N, P, n) configurations and prints a table.
# Output saved to results_q1.txt and printed to terminal.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RESULTS="results_q1.txt"

# Configurations: "N P n"
CONFIGS=(
    "16    1   100000"
    "16    2   50000"
    "16    4   25000"
    "16    8   12500"
    "16   16    6250"
    "16   32    3125"
    "64    1   100000"
    "64    2   50000"
    "64    4   25000"
    "64    8   12500"
    "64   16    6250"
    "64   32    3125"
    "256   1   100000"
    "256   2   50000"
    "256   4   25000"
    "256   8   12500"
    "256  16    6250"
    "256  32    3125"
)

PROGRAMS=(P1 P2 P3)
COL=22

SEP=$(printf '+%s' $(printf -- '-%.0s' $(seq 1 6)))
SEP+=$(printf '+%s' $(printf -- '-%.0s' $(seq 1 6)))
SEP+=$(printf '+%s' $(printf -- '-%.0s' $(seq 1 8)))
for P in "${PROGRAMS[@]}"; do SEP+=$(printf '+%s' $(printf -- '-%.0s' $(seq 1 $((COL+1))))); done
SEP+="+"

print_header() {
    echo "$SEP"
    printf "| %-4s | %-4s | %-6s " "N" "P" "n"
    for P in "${PROGRAMS[@]}"; do printf "| %-${COL}s" "$P"; done
    printf "|\n"
    echo "$SEP"
}

print_row() {
    local N=$1 Pv=$2 n=$3; shift 3
    printf "| %-4s | %-4s | %-6s " "$N" "$Pv" "$n"
    for val in "$@"; do printf "| %-${COL}s" "$val"; done
    printf "|\n"
}

# ---------------------------------------------------------------------------
# Compile
# ---------------------------------------------------------------------------
echo "Compiling q1.cpp ..."
g++ -O3 -pthread -static-libstdc++ q1.cpp -o q1
if [ $? -ne 0 ]; then
    echo "Compilation failed." >&2
    exit 1
fi
echo "Done."
echo ""

run_and_print() {
    echo "========================================================================"
    echo " Q1 Benchmark (P1=lock-free sem, P2=POSIX sem, P3=cond-var)"
    echo " $(date)"
    echo "========================================================================"

    TMPDIR_RUN=$(mktemp -d)

    # Run all configs upfront.
    # stdout = consumed-value list (discarded so I/O does not skew timing).
    # stderr = timing lines (P1/P2/P3 Time + checksum) — captured for parsing.
    IDX=0
    for CFG in "${CONFIGS[@]}"; do
        read -r N Pv n <<< "$CFG"
        ./q1 "$N" "$Pv" "$n" > /dev/null 2>"$TMPDIR_RUN/out_${IDX}.txt"
        IDX=$((IDX+1))
    done

    # Helper: extract time for program label (P1/P2/P3) from an output file
    get_time() {
        local label=$1 file=$2
        case "$label" in
            P1) grep "^P1" "$file" | sed 's/.*Time = \([0-9]*\) us.*/\1/' ;;
            P2) grep "^P2" "$file" | sed 's/.*Time = \([0-9]*\) us.*/\1/' ;;
            P3) grep "^P3" "$file" | sed 's/.*Time = \([0-9]*\) us.*/\1/' ;;
        esac
    }

    get_checksum() {
        local label=$1 file=$2
        case "$label" in
            P1) grep "^P1" "$file" | sed 's/.*checksum = \([0-9]*\)/\1/' ;;
            P2) grep "^P2" "$file" | sed 's/.*checksum = \([0-9]*\)/\1/' ;;
            P3) grep "^P3" "$file" | sed 's/.*checksum = \([0-9]*\)/\1/' ;;
        esac
    }

    # ---- Time table (grouped by N) ------------------------------------------
    prev_N=""
    for buf_size in 16 64 256; do
        echo ""
        echo "########################################################################"
        echo "# Buffer size N = $buf_size"
        echo "########################################################################"
        echo ""
        echo "Time (microseconds):"
        print_header

        IDX=0
        for CFG in "${CONFIGS[@]}"; do
            read -r N Pv n <<< "$CFG"
            if [ "$N" -eq "$buf_size" ]; then
                outfile="$TMPDIR_RUN/out_${IDX}.txt"
                vals=()
                for Prog in "${PROGRAMS[@]}"; do
                    t=$(get_time "$Prog" "$outfile")
                    vals+=("${t:-N/A} us")
                done
                print_row "$N" "$Pv" "$n" "${vals[@]}"
            fi
            IDX=$((IDX+1))
        done
        echo "$SEP"

        # ---- Checksum table (verify correctness) --------------------------------
        echo ""
        echo "Checksums (must match across P1/P2/P3):"
        print_header

        IDX=0
        for CFG in "${CONFIGS[@]}"; do
            read -r N Pv n <<< "$CFG"
            if [ "$N" -eq "$buf_size" ]; then
                outfile="$TMPDIR_RUN/out_${IDX}.txt"
                vals=()
                for Prog in "${PROGRAMS[@]}"; do
                    cs=$(get_checksum "$Prog" "$outfile")
                    vals+=("${cs:-N/A}")
                done
                print_row "$N" "$Pv" "$n" "${vals[@]}"
            fi
            IDX=$((IDX+1))
        done
        echo "$SEP"

        # ---- Speedup vs P1 baseline (1 producer) --------------------------------
        echo ""
        echo "Speedup: P=1 time / P=T time  (per synchronisation method):"
        print_header

        # Find baseline (P=1) indices for this N
        declare -A base_time
        IDX=0
        for CFG in "${CONFIGS[@]}"; do
            read -r N Pv n <<< "$CFG"
            if [ "$N" -eq "$buf_size" ] && [ "$Pv" -eq 1 ]; then
                outfile="$TMPDIR_RUN/out_${IDX}.txt"
                for Prog in "${PROGRAMS[@]}"; do
                    base_time["$Prog"]=$(get_time "$Prog" "$outfile")
                done
            fi
            IDX=$((IDX+1))
        done

        IDX=0
        for CFG in "${CONFIGS[@]}"; do
            read -r N Pv n <<< "$CFG"
            if [ "$N" -eq "$buf_size" ]; then
                outfile="$TMPDIR_RUN/out_${IDX}.txt"
                vals=()
                for Prog in "${PROGRAMS[@]}"; do
                    base="${base_time[$Prog]}"
                    curr=$(get_time "$Prog" "$outfile")
                    if [[ -n "$base" && -n "$curr" && "$curr" -gt 0 ]] 2>/dev/null; then
                        sp=$(awk "BEGIN {printf \"%.2fx\", $base/$curr}")
                    else
                        sp="N/A"
                    fi
                    vals+=("$sp")
                done
                print_row "$N" "$Pv" "$n" "${vals[@]}"
            fi
            IDX=$((IDX+1))
        done
        echo "$SEP"
        unset base_time
        declare -A base_time
    done

    rm -rf "$TMPDIR_RUN"

    echo ""
    echo "========================================================================"
    echo " Done."
    echo "========================================================================"
}

run_and_print 2>&1 | tee "$RESULTS"

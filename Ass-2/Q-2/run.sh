#!/usr/bin/env bash
# Compile and benchmark q2 (P1-P6).
# Prints 3 tables (one per ops file) — Time, Throughput, Speedup.
# Output saved to results.txt and printed to terminal.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

GRAPH="inputs/A2_Q2_graph_N=1000_M=5000.txt"
OPS_FILES=(
    "inputs/A2_Q2_N=1000_ops=16000_1.txt"
    "inputs/A2_Q2_N=1000_ops=16000_2.txt"
    "inputs/A2_Q2_N=1000_ops=16000_3.txt"
)
NUM_OPS=16000
RESULTS="results.txt"
THREADS=(1 2 4 8 16 32)
PROGRAMS=(P1 P2 P3 P4 P5 P6)

# column width per program
COL=20
SEP_INNER=$(printf '+%s' $(printf -- '-%.0s' $(seq 1 10)))
for P in "${PROGRAMS[@]}"; do SEP_INNER+=$(printf '+%s' $(printf -- '-%.0s' $(seq 1 $((COL+1))))); done
SEP_INNER+="+"

print_header() {
    echo "$SEP_INNER"
    printf "| %-8s " "Threads"
    for P in "${PROGRAMS[@]}"; do printf "| %-${COL}s" "$P"; done
    printf "|\n"
    echo "$SEP_INNER"
}

print_row() {
    local T=$1; shift
    printf "| %-8s " "$T"
    for val in "$@"; do printf "| %-${COL}s" "$val"; done
    printf "|\n"
}

# ---------------------------------------------------------------------------
# Compile
# ---------------------------------------------------------------------------
echo "Compiling q2.cpp ..."
g++ -O3 -pthread -static-libstdc++ q2.cpp -o q2
echo "Done."
echo ""

run_and_print() {
    echo "========================================================================"
    echo " Q2 Benchmark (P1-P6)  —  $(date)"
    echo " Graph : $GRAPH  |  Ops : $NUM_OPS"
    echo "========================================================================"

    for OPS_IDX in 0 1 2; do
        OPS_FILE="${OPS_FILES[$OPS_IDX]}"

        echo ""
        echo "########################################################################"
        printf "# Table %d : %s\n" $((OPS_IDX+1)) "$OPS_FILE"
        echo "########################################################################"

        # ----- collect raw outputs for all thread counts -------------------------
        TMPDIR_RUN=$(mktemp -d)
        for T in "${THREADS[@]}"; do
            ./q2 "$GRAPH" "$OPS_FILE" "$NUM_OPS" "$T" > "$TMPDIR_RUN/out_$T.txt" 2>&1
        done

        # helper: extract field for program P and thread count T
        get_time() { grep "^$1 " "$TMPDIR_RUN/out_$2.txt" | sed 's/.*Time: \([0-9]*\) us.*/\1/'; }
        get_tp()   { grep "^$1 " "$TMPDIR_RUN/out_$2.txt" | sed 's/.*Throughput: \([0-9.]*\) ops.*/\1/'; }

        # ----- Time table --------------------------------------------------------
        echo ""
        echo "Time (microseconds):"
        print_header
        for T in "${THREADS[@]}"; do
            vals=()
            for P in "${PROGRAMS[@]}"; do
                vals+=("$(get_time $P $T)")
            done
            print_row "$T" "${vals[@]}"
        done
        echo "$SEP_INNER"

        # ----- Throughput table --------------------------------------------------
        echo ""
        echo "Throughput (ops/sec):"
        print_header
        for T in "${THREADS[@]}"; do
            vals=()
            for P in "${PROGRAMS[@]}"; do
                vals+=("$(get_tp $P $T)")
            done
            print_row "$T" "${vals[@]}"
        done
        echo "$SEP_INNER"

        # ----- Speedup table (Time_1 / Time_T per program) -----------------------
        echo ""
        echo "Speedup (Time_1thread / Time_Nthreads, per program):"
        print_header
        for T in "${THREADS[@]}"; do
            vals=()
            for P in "${PROGRAMS[@]}"; do
                base=$(get_time $P 1)
                curr=$(get_time $P $T)
                if [[ -n "$base" && -n "$curr" && "$curr" -gt 0 ]] 2>/dev/null; then
                    sp=$(awk "BEGIN {printf \"%.2fx\", $base/$curr}")
                else
                    sp="N/A"
                fi
                vals+=("$sp")
            done
            print_row "$T" "${vals[@]}"
        done
        echo "$SEP_INNER"

        rm -rf "$TMPDIR_RUN"
    done

    echo ""
    echo "========================================================================"
    echo " Done."
    echo "========================================================================"
}

run_and_print 2>&1 | tee "$RESULTS"

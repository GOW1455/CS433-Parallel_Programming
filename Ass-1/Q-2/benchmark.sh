#!/usr/bin/env bash

set -u

EXEC="./q2"
INPUT_DIR="./inputs"
OUTPUT_DIR="./outputs"
N_VALUES=(512 1024 2048 4096 8192)
THREADS=(1 2 4 8 16)
RUNS=5

if [ ! -x "$EXEC" ]; then
    echo "Error: executable $EXEC was not found or is not executable."
    echo "Compile it first with:"
    echo "  g++ -std=c++11 -O2 -fopenmp main_q2.cpp -o main_q2"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

echo "Q2 benchmark: ${RUNS} runs per n/thread combination"
echo "Time is reported in microseconds; error is max ||Ax - y||_inf."
echo
printf "%-8s | %-8s | %-18s | %-10s | %-18s\n" \
    "n" "Threads" "Average time (us)" "Speedup" "Average error"
printf "%-8s-+-%-8s-+-%-18s-+-%-10s-+-%-18s\n" \
    "--------" "--------" "------------------" "----------" "------------------"

for N_VAL in "${N_VALUES[@]}"; do
    baseline_time=""
    input_file="$INPUT_DIR/A1_Q2_N=$N_VAL.txt"
    if [ ! -f "$input_file" ]; then
        echo "Error: input file not found: $input_file"
        exit 1
    fi
    file_n=$(awk 'NR == 1 {print $1; exit}' "$input_file")
    if [ "$file_n" != "$N_VAL" ]; then
        echo "Error: invalid input file: $input_file"
        echo "The first line must contain n=$N_VAL, but found: ${file_n:-<empty>}"
        echo "Generate it with:"
        echo "  ./inputs/A1_Q2_generate_inputs $N_VAL"
        exit 1
    fi

    for T in "${THREADS[@]}"; do
        total_time=0
        total_error=0

        for ((run = 1; run <= RUNS; run++)); do
            output_file="$OUTPUT_DIR/A1_Q2_N=${N_VAL}_T=${T}_run=${run}.txt"
            output=$("$EXEC" "$input_file" "$output_file" "$T" 2>&1)
            status=$?
            if [ "$status" -ne 0 ]; then
                echo "Error: $EXEC failed for n=$N_VAL, threads=$T, run=$run"
                echo "$output"
                exit "$status"
            fi

            time_val=$(printf '%s\n' "$output" |
                awk '/^Time:/ {print $2; exit}')
            error_val=$(printf '%s\n' "$output" |
                awk '/^Max residual/ {print $NF; exit}')

            if [ -z "$time_val" ] || [ -z "$error_val" ]; then
                echo "Error: could not parse time/error for n=$N_VAL, threads=$T, run=$run"
                echo "$output"
                exit 1
            fi

            total_time=$(awk -v total="$total_time" -v value="$time_val" \
                'BEGIN { printf "%.12f", total + value }')
            total_error=$(awk -v total="$total_error" -v value="$error_val" \
                'BEGIN { printf "%.12f", total + value }')
        done

        avg_time=$(awk -v total="$total_time" -v runs="$RUNS" \
            'BEGIN { printf "%.3f", total / runs }')
        avg_error=$(awk -v total="$total_error" -v runs="$RUNS" \
            'BEGIN { printf "%.12e", total / runs }')

        if [ "$T" -eq 1 ]; then
            baseline_time="$avg_time"
        fi

        speedup=$(awk -v baseline="$baseline_time" -v current="$avg_time" \
            'BEGIN {
                if (current > 0) printf "%.3f", baseline / current;
                else printf "N/A";
            }')

        printf "%-8s | %-8s | %-18s | %-10s | %-18s\n" \
            "$N_VAL" "$T" "$avg_time" "$speedup" "$avg_error"
    done
    echo "--------------------------------------------------------------------------"
done
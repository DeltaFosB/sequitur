#!/bin/bash

# Ensure the script is run with sudo privileges for real-time scheduling
if [ "$EUID" -ne 0 ]; then
  echo "Error: Please run this script with sudo (required for chrt real-time priority)."
  exit 1
fi

# Configuration
BENCHMARK_BIN="./benchmarks"
NUM_RUNS=10
CPU_CORE=3
PRIORITY=99

if [ ! -f "$BENCHMARK_BIN" ]; then
    echo "Error: $BENCHMARK_BIN not found. Please build the target first."
    exit 1
fi

echo "======================================================="
echo "   SEQUITUR HARDWARE EXECUTOR AGGREGATED BENCHMARK"
echo "======================================================="
echo "Config: Running $NUM_RUNS iterations on Core $CPU_CORE (SCHED_FIFO:$PRIORITY)"
echo "-------------------------------------------------------"

total_avg=0
total_p99=0
avg_list=()
p99_list=()

for ((i=1; i<=NUM_RUNS; i++)); do
    # Run the benchmark with real-time pinning and isolate metrics
    output=$(chrt -f $PRIORITY taskset -c $CPU_CORE $BENCHMARK_BIN 2>/dev/null)
    
    # Parse out the raw numeric values using awk
    run_avg=$(echo "$output" | grep "Pure Macro Average Latency:" | awk '{print $5}')
    run_p99=$(echo "$output" | grep "P99 Batch Latency:" | awk '{print $4}')
    
    if [ -z "$run_avg" ] || [ -z "$run_p99" ]; then
        echo "Run $(printf "%02d" $i)/$NUM_RUNS: Failed to capture metrics."
        continue
    fi
    
    avg_list+=("$run_avg")
    p99_list+=("$run_p99")
    
    total_avg=$(echo "$total_avg + $run_avg" | bc)
    total_p99=$(echo "$total_p99 + $run_p99" | bc)
    
    echo "Run $(printf "%02d" $i)/$NUM_RUNS: Avg = $run_avg ns | P99 = $run_p99 ns"
done

# Calculate Means
mean_avg=$(echo "scale=4; $total_avg / $NUM_RUNS" | bc)
mean_p99=$(echo "scale=4; $total_p99 / $NUM_RUNS" | bc)

# Calculate Variance and Standard Deviation for Average Latency
variance_avg=0
for val in "${avg_list[@]}"; do
    diff=$(echo "$val - $mean_avg" | bc)
    sq_diff=$(echo "$diff * $diff" | bc)
    variance_avg=$(echo "$variance_avg + $sq_diff" | bc)
done
std_dev_avg=$(echo "scale=4; sqrt($variance_avg / $NUM_RUNS)" | bc)

# Calculate Variance and Standard Deviation for P99 Latency
variance_p99=0
for val in "${p99_list[@]}"; do
    diff=$(echo "$val - $mean_p99" | bc)
    sq_diff=$(echo "$diff * $diff" | bc)
    variance_p99=$(echo "$variance_p99 + $sq_diff" | bc)
done
std_dev_p99=$(echo "scale=4; sqrt($variance_p99 / $NUM_RUNS)" | bc)

echo "-------------------------------------------------------"
echo "AGGREGATED STATISTICAL SUMMARY ($NUM_RUNS Runs):"
echo "-------------------------------------------------------"
echo "  MACRO AVERAGE LATENCY:"
echo "    Mean Average Latency : $mean_avg ns"
echo "    Hardware Jitter      : $std_dev_avg ns"
echo ""
echo "  TAIL PROFILE (P99 LATENCY):"
echo "    Mean P99 Latency     : $mean_p99 ns"
echo "    Tail Jitter          : $std_dev_p99 ns"
echo "======================================================="

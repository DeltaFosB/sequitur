#!/usr/bin/env bash

# ==============================================================================
# Sequitur High-Frequency Trading Engine End-to-End Orchestrator (run_n2n.sh)
# ==============================================================================

set -e

PROJECT_ROOT="/home/rajv/Desktop/Programming/sequitur"
cd "${PROJECT_ROOT}"

clear
echo "======================================================================"
echo "    SEQUITUR HIGH-VELOCITY TRADING ENGINE AUTOMATED TEST RUNNER       "
echo "======================================================================"
echo ""

echo "[Phase 1/5] Executing Aggressive Infrastructure Environment Purge..."
sudo pkill -15 sequitur_engine || true
pkill -9 main || true

# Force unlink active POSIX shared memory allocations and old telemetry routes
sudo rm -f /dev/shm/sequitur_shm
sudo rm -rf /dev/shm/sequitur

# Re-initialize clean RAM-disk logging folders
sudo mkdir -p /dev/shm/sequitur
sudo touch /dev/shm/sequitur/telemetry.log
sudo chmod 666 /dev/shm/sequitur/telemetry.log

# Wipe legacy Vector fingerprint cache databases completely
if [ -d "./.vector_data" ]; then
    echo "[-] Wiping out legacy Vector fingerprint cache databases..."
    sudo rm -rf ./.vector_data/*
fi
echo "[OK] Core environments reset."
echo ""

echo "[Phase 2/5] Running High-Performance Parallel C++ Compilation..."
cmake --build build --parallel "$(nproc)" --target sequitur_engine
echo "[OK] Binaries rebuilt."
echo ""

echo "[Phase 3/5] Verifying Telemetry Infrastructure..."
# Boot up Loki, Grafana, and Vector simultaneously
docker compose up -d
echo "[-] Awaiting Grafana service stabilization..."
while ! nc -z localhost 3000; do   
    sleep 0.5
done
echo "[OK] Telemetry stack online."
echo ""

echo "[Phase 4/5] Activating Go Gateway and Binding Real-Time C++ Engine..."
cd gateway
# Run gateway and background its execution instance
go run main.go shm.go protocol.go &
GATEWAY_PID=$!
cd "${PROJECT_ROOT}"

# CRITICAL: Wait for Go gateway to build the lock-free shared memory map
echo "[-] Waiting for shared memory segment to be initialized by Gateway..."
MAX_RETRIES=20
COUNT=0
while [ ! -f /dev/shm/sequitur_shm ]; do
    sleep 0.5
    COUNT=$((COUNT+1))
    if [ $COUNT -ge $MAX_RETRIES ]; then
        echo "[!] Error: Gateway failed to initialize shared memory."
        kill $GATEWAY_PID
        exit 1
    fi
done
echo "[OK] Shared memory segment detected."

# Spawn the RAM Protection Sentinel Loop in the background.
(
    while [ -f /dev/shm/sequitur/telemetry.log ]; do
        FILE_SIZE=$(stat -c%s "/dev/shm/sequitur/telemetry.log" 2>/dev/null || echo 0)
        # Short-circuit logic: No 'if', no 'then', no 'fi'
        [ "$FILE_SIZE" -gt 10485760 ] && truncate -s 0 /dev/shm/sequitur/telemetry.log
        sleep 1
    done
) &
SENTINEL_PID=$!

# Launch C++ Engine on isolated CPU Cores 3 and 4 with Real-Time FIFO priority
sudo taskset -c 3,4 chrt -f 99 ./build/sequitur_engine >> /dev/shm/sequitur/telemetry.log 2>&1 &
ENGINE_PID=$!
echo "[OK] C++ Engine core engaged on CPU Cores 3 and 4."
echo ""

echo "======================================================================"
echo "INJECTION MATRIX READY: PLATFORM MONITORING LINKS GENERATED"
echo "======================================================================"
echo "  MONITORING DASHBOARD LINK: http://localhost:3000/d/sequitur-hft-monitor"
echo "======================================================================"
echo ""

cleanup() {
    echo ""
    echo "[!] Termination sequence intercepted..."
    kill -9 ${SENTINEL_PID} 2>/dev/null || true
    sudo pkill -9 ${ENGINE_PID} 2>/dev/null || true
    sudo pkill -9 sequitur_engine 2>/dev/null || true
    kill -9 ${GATEWAY_PID} 2>/dev/null || true
    # Bring down telemetry system metrics containers
    docker compose down 2>/dev/null || true
    sudo rm -rf /dev/shm/sequitur
    echo "[OK] Pipelines offline."
}
trap cleanup EXIT

echo "[Phase 5/5] Injecting Continuous Market Traffic Simulation Loops..."
# Execute the continuous traffic injection framework synchronously
python3 client.py

# FIXED: Prevent the script from exiting and destroying the Loki database
echo ""
echo "======================================================================"
echo " SIMULATION COMPLETE. DATA IS IN LOKI."
echo " Check Grafana now. Press [ENTER] to tear down the infrastructure."
echo "======================================================================"
read -p ""

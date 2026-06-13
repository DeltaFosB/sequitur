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

# Bring down running telemetry system containers first to release file descriptors
docker compose down 2>/dev/null || true

# Force unlink active POSIX shared memory allocations and old telemetry routes
sudo rm -f /dev/shm/sequitur_shm
sudo rm -rf /dev/shm/sequitur

# Re-initialize clean RAM-disk logging folders
sudo mkdir -p /dev/shm/sequitur
sudo touch /dev/shm/sequitur/telemetry.log
sudo chmod 666 /dev/shm/sequitur/telemetry.log

# Wipe legacy Vector fingerprint cache databases completely to prevent indexing offsets
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
# Boot up Loki, Grafana, and Vector simultaneously with clean data volumes
docker compose up -d
echo "[-] Awaiting Grafana service stabilization..."
while ! nc -z localhost 3000; do   
    sleep 0.5
done
echo "[OK] Telemetry stack online."
echo ""

echo "[Phase 4/5] Activating Go Gateway and Binding Real-Time C++ Engine..."
cd gateway
go run main.go shm.go protocol.go &
GATEWAY_PID=$!
cd "${PROJECT_ROOT}"

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

# Core 2: Telemetry (Priority 50) | Core 3: Matching (Priority 99) | Core 4: Egress (Priority 99)
sudo taskset -c 2,3,4 chrt -f 99 stdbuf -o0 -e0 ./build/sequitur_engine >> /dev/shm/sequitur/telemetry.log 2>&1 &

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
    sudo pkill -9 ${ENGINE_PID} 2>/dev/null || true
    sudo pkill -9 sequitur_engine 2>/dev/null || true
    kill -9 ${GATEWAY_PID} 2>/dev/null || true
    docker compose down 2>/dev/null || true
    sudo rm -rf /dev/shm/sequitur
    echo "[OK] Pipelines offline."
}
trap cleanup EXIT

echo "[Phase 5/5] Injecting Continuous Market Traffic Simulation Loops..."
python3 client.py

echo ""
echo "======================================================================"
echo " SIMULATION COMPLETE. DATA IS IN LOKI."
echo " Check Grafana now. Press [ENTER] to tear down the infrastructure."
echo "======================================================================"
read -p ""

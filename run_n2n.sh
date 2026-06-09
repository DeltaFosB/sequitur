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

# Force unlink the active POSIX shared memory ring buffer
sudo rm -f /dev/shm/sequitur_shm

# Flush RAM-disk file paths
sudo mkdir -p /dev/shm/sequitur
sudo touch /dev/shm/sequitur/telemetry.log
sudo chmod 666 /dev/shm/sequitur/telemetry.log

# Fix globbing issue by removing quotes
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
docker compose up -d
echo "[-] Awaiting Grafana service stabilization..."
while ! nc -z localhost 3000; do   
  sleep 0.5
done
echo "[OK] Telemetry stack online."
echo ""

echo "[Phase 4/5] Activating Go Gateway and Binding Real-Time C++ Engine..."
cd gateway
# Run gateway and background it
go run main.go shm.go protocol.go &
GATEWAY_PID=$!
cd "${PROJECT_ROOT}"

# CRITICAL: Wait for Go gateway to create the shared memory file
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

# Launch C++ Engine on Core 3 with RT priority
sudo taskset -c 3 chrt -f 99 ./build/sequitur_engine >> /dev/shm/sequitur/telemetry.log 2>&1 &
ENGINE_PID=$!
echo "[OK] C++ Engine core engaged on CPU Core 3."
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
    sudo kill -15 ${ENGINE_PID} 2>/dev/null || true
    kill -9 ${GATEWAY_PID} 2>/dev/null || true
    echo "[OK] Pipelines offline."
}
trap cleanup EXIT

echo "[Phase 5/5] Injecting Continuous Market Traffic Simulation Loops..."
python3 client.py

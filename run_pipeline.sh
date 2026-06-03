#!/usr/bin/env bash

# Ensure the script is run with root/sudo privileges (required for chrt real-time FIFO)
if [ "$EUID" -ne 0 ]; then
    echo "Error: Please run this script with sudo privileges."
    echo "Usage: sudo $0"
    exit 1
fi

# Store the project root directory
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_ROOT" || exit 1

# Function to perform clean shutdown of background tasks on interruption
cleanup() {
    echo -e "\n[Shutdown] Tearing down telemetry pipeline..."
    pkill -f vector
    docker compose down
    echo "[Shutdown] Clean exit complete."
    exit 0
}

# Trap Ctrl+C (SIGINT) and SIGTERM to execute the cleanup procedure
trap cleanup SIGINT SIGTERM

echo "[Initialization] Establishing RAM disk shared memory space..."
mkdir -p /dev/shm/sequitur
rm -f /dev/shm/sequitur/telemetry.log
touch /dev/shm/sequitur/telemetry.log
chmod 666 /dev/shm/sequitur/telemetry.log

echo "[Initialization] Launching Docker Core Containers (Loki + Grafana)..."
docker compose up -d

echo "[Initialization] Clearing old Vector metric checkpoints..."
pkill -f vector
rm -rf ./.vector_data/*

echo "[Initialization] Spawning Vector Telemetry Daemon..."
vector --config ./vector.toml >/dev/null 2>&1 &

echo "[Initialization] Allowing network ports to settle..."
sleep 3

echo "================================================================"
echo " Sequitur Real-Time Hardware Performance Monitor Active"
echo "================================================================"
echo " -> Engine Thread Affinity: CPU Core 3"
echo " -> Scheduling Class:      SCHED_FIFO (Priority 99)"
echo " -> Telemetry Output:      /dev/shm/sequitur/telemetry.log"
echo " -> Grafana View Dashboard: http://localhost:3000"
echo "================================================================"
echo "Press [Ctrl+C] at any time to terminate the simulation and docker containers."

# Infinite execution simulation loop
while true; do
    # Force unbuffered streaming, real-time priority, and Core 3 pinning
    stdbuf -o0 chrt -f 99 taskset -c 3 ./build/sequitur_engine >> /dev/shm/sequitur/telemetry.log 2>&1
    sleep 1
done

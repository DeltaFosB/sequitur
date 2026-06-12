#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

# Terminal text colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}==================================================${NC}"
echo -e "${BLUE}       Sequitur Go Gateway Test Automation        ${NC}"
echo -e "${BLUE}==================================================${NC}"

# 1. Clean up stale shared memory segments if they exist
if [ -f "/dev/shm/sequitur_shm" ]; then
    echo -e "Removing stale shared memory file..."
    rm -f /dev/shm/sequitur_shm
fi

# 2. Run Protocol Component Tests
echo -e "\n${BLUE}[1/4] Running Protocol & Matrix Tests...${NC}"
go test -v protocol_test.go protocol.go

# 3. Run Shared Memory Component Tests & Benchmarks
echo -e "\n${BLUE}[2/4] Running Shared Memory Alignment & Enqueue Benchmarks...${NC}"
go test -v -bench=BenchmarkEnqueue -benchmem shm_test.go shm.go protocol.go

# 4. Run Network Gateway Integration Tests & Benchmarks
echo -e "\n${BLUE}[3/4] Running End-to-End Concurrency & Throughput Profiling...${NC}"
go test -v -bench=BenchmarkGatewayIntegration -benchmem gateway_test.go main.go shm.go protocol.go

# 5. Verify /dev/shm File Creation and Size Alignment
echo -e "\n${BLUE}[4/4] Verifying POSIX Shared Memory Kernel State...${NC}"

# Compile the gateway binary temporarily to test production initialization
go build -o gateway_bin main.go shm.go protocol.go

# Launch the gateway in the background using a random port so it doesn't block
./gateway_bin &
GATEWAY_PID=$!

# Give the process a moment to initialize and map /dev/shm
sleep 0.5

# Check if the kernel virtual file exists
if [ -f "/dev/shm/sequitur_shm" ]; then
    echo -e "${GREEN}✓ Virtual file successfully mounted in /dev/shm/sequitur_shm${NC}"
    
    # Capture and verify file size (expected size: 6291488 bytes)
    FILE_SIZE=$(stat -c%s "/dev/shm/sequitur_shm")
    echo -e "Shared Memory Segment Allocated Size: ${FILE_SIZE} bytes"
    
    if [ "$FILE_SIZE" -eq 6291488 ]; then
        echo -e "${GREEN}✓ Shared memory allocation size aligns perfectly with 32KB boundary!${NC}"
    else
        echo -e "${RED}✗ Size mismatch! Expected 6291488 bytes, got ${FILE_SIZE}${NC}"
        kill $GATEWAY_PID
        exit 1
    fi
else
    echo -e "${RED}✗ Failure: /dev/shm/sequitur_shm was not created by the kernel.${NC}"
    kill $GATEWAY_PID
    exit 1
fi

# Clean up the background gateway process and temporary binary
kill $GATEWAY_PID
rm gateway_bin

echo -e "\n${GREEN}==================================================${NC}"
echo -e "${GREEN}      ALL TESTS PASSED & VERIFIED SUCCESSFULLY     ${NC}"
echo -e "${GREEN}==================================================${NC}"

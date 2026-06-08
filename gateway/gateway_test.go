package main

import (
	"fmt"
	"net"
	"sync"
	"sync/atomic"
	"testing"
	"time"
	"unsafe"
)

func TestGatewayIntegration(t *testing.T) {
	// 1. Setup isolated, in-memory SharedMemory spaces to prevent production /dev/shm pollution
	backingBuffer := make([]byte, totalSize)
	var shm SharedMemory
	shm.read_idx = (*int64)(unsafe.Pointer(&backingBuffer[0]))
	shm.write_idx = (*int64)(unsafe.Pointer(&backingBuffer[8]))
	shm.buffer = uintptr(unsafe.Pointer(&backingBuffer[16]))

	atomic.StoreInt64(shm.read_idx, 0)
	atomic.StoreInt64(shm.write_idx, 0)

	packetChan := make(chan IngressPacket, 1024)

	// 2. Bind the network listener to an ephemeral random port (127.0.0.1:0)
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("Failed to bind local test socket: %v", err)
	}
	serverAddr := listener.Addr().String()

	// 3. Launch your production engine block.
	// Since tests don't have a C++ reader consuming data, we spin up a tiny background goroutine
	// that continuously clears the read index so our Enqueue checks never hit a full wall.
	go func() {
		for {
			currWrite := atomic.LoadInt64(shm.write_idx)
			atomic.StoreInt64(shm.read_idx, currWrite)
			time.Sleep(10 * time.Microsecond)
		}
	}()

	// Start the actual gateway processing loops
	go ServeGateway(listener, &shm, packetChan)

	// 4. Configure our concurrent stress parameters
	const numClients = 5
	const packetsPerClient = 100
	var wg sync.WaitGroup

	mockCSVRow := "1,2,1005,5002,10,150450,999101\n"

	// 5. Spawn the concurrent client fleet
	for clientID := range numClients {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()

			// Establish an isolated TCP link to our active ServeGateway engine
			conn, err := net.Dial("tcp", serverAddr)
			if err != nil {
				t.Errorf("Client %d failed to connect: %v", id, err)
				return
			}
			defer conn.Close()

			// Stream packets as fast as the network interface can ingest them
			for range packetsPerClient {
				_, err := fmt.Fprint(conn, mockCSVRow)
				if err != nil {
					t.Errorf("Client %d failed to write packet: %v", id, err)
					return
				}
			}
		}(clientID)
	}

	// Wait for all concurrent workers to close their connections completely
	wg.Wait()

	// Give the network processing stack a tiny grace window to clear out any remaining channel buffers
	time.Sleep(50 * time.Millisecond)

	// 6. Force the engine to stop by closing the network listener safely
	listener.Close()

	// 7. Core Invariant Check: Verify total multiplexed processing volume
	expectedTotal := int64(numClients * packetsPerClient)
	finalProcessed := atomic.LoadInt64(shm.write_idx)

	if finalProcessed != expectedTotal {
		t.Errorf("Concurrency processing failure! Expected total packets in shared memory to be %d, but got %d", expectedTotal, finalProcessed)
	}
}

func BenchmarkGatewayIntegration(b *testing.B) {
	// 1. Setup sandboxed, in-memory SharedMemory tracking
	backingBuffer := make([]byte, totalSize)
	var shm SharedMemory
	shm.read_idx = (*int64)(unsafe.Pointer(&backingBuffer[0]))
	shm.write_idx = (*int64)(unsafe.Pointer(&backingBuffer[8]))
	shm.buffer = uintptr(unsafe.Pointer(&backingBuffer[16]))

	atomic.StoreInt64(shm.read_idx, 0)
	atomic.StoreInt64(shm.write_idx, 0)

	packetChan := make(chan IngressPacket, 4096)

	// Keep shared memory cleared so the gateway never hits a full buffer block
	go func() {
		for {
			currWrite := atomic.LoadInt64(shm.write_idx)
			atomic.StoreInt64(shm.read_idx, currWrite)
		}
	}()

	// 2. Start the core server engine background thread
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		b.Fatalf("Failed to bind benchmark socket: %v", err)
	}
	defer listener.Close()

	go ServeGateway(listener, &shm, packetChan)

	// 3. Establish a single dedicated streaming connection
	conn, err := net.Dial("tcp", listener.Addr().String())
	if err != nil {
		b.Fatalf("Benchmark client failed to connect: %v", err)
	}
	defer conn.Close()

	mockCSVRow := []byte("1,2,1005,5002,10,150450,999101\n")

	b.ResetTimer()

	// 4. Blast data continuously down the network pipe
	for i := 0; i < b.N; i++ {
		_, err := conn.Write(mockCSVRow)
		if err != nil {
			b.Fatalf("Network write failure at iteration %d: %v", i, err)
		}
	}

	// Grace window for the final packets to slide through channels into shm
	time.Sleep(10 * time.Millisecond)
}

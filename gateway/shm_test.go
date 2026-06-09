package main

import (
	"sync/atomic"
	"testing"
	"unsafe"
)

func TestRingBufferFull(t *testing.T) {
	// 1. Manually allocate an isolated, in-memory backing buffer to avoid clobbering production /dev/shm
	backingBuffer := make([]byte, totalSize)

	var shm SharedMemory
	shm.read_idx = (*int64)(unsafe.Pointer(&backingBuffer[0]))
	shm.write_idx = (*int64)(unsafe.Pointer(&backingBuffer[8]))
	shm.buffer = uintptr(unsafe.Pointer(&backingBuffer[16]))

	// 2. Artificially reset our queue pointers to absolute zero using atomic primitives
	atomic.StoreInt64(shm.read_idx, 0)
	atomic.StoreInt64(shm.write_idx, 0)

	// Create a generic mock packet to stream into the slots
	mockPacket := IngressPacket{
		Type: 1, Side: 2, InstrumentID: 100, TraderID: 50,
		Quantity: 10, Price: 50000, ClientOrderID: 12345,
	}

	// 3. Loop exactly 1024 times to fill every single slot in the ring buffer array
	for i := 0; i < int(SIZE); i++ {
		success := shm.Enqueue(mockPacket)
		if !success {
			t.Fatalf("Queue failed prematurely at index %d; expected true but got false", i)
		}
	}

	// 4. Attempt a 1025th Enqueue and assert that it handles boundary saturation correctly
	overfillSuccess := shm.Enqueue(mockPacket)
	if overfillSuccess {
		t.Errorf("Security boundary breached! Expected Enqueue to return false when ring buffer is completely full, but got true")
	}
}

func BenchmarkEnqueue(b *testing.B) {
	// 1. Setup isolated, in-memory shm and an IngressPacket instance
	backingBuffer := make([]byte, totalSize)

	var shm SharedMemory
	shm.read_idx = (*int64)(unsafe.Pointer(&backingBuffer[0]))
	shm.write_idx = (*int64)(unsafe.Pointer(&backingBuffer[8]))
	shm.buffer = uintptr(unsafe.Pointer(&backingBuffer[16]))

	mockPacket := IngressPacket{
		Type: 1, Side: 2, InstrumentID: 100, TraderID: 50,
		Quantity: 10, Price: 50000, ClientOrderID: 12345,
	}

	// Reset indices cleanly before beginning the test
	atomic.StoreInt64(shm.read_idx, 0)
	atomic.StoreInt64(shm.write_idx, 0)

	b.ResetTimer()

	// 2. Loop exactly b.N times
	for i := 0; i < b.N; i++ {
		// 3. Keep the queue clear by dynamically sliding the read index
		// right behind the current write index to bypass the full guard.
		currWrite := atomic.LoadInt64(shm.write_idx)
		atomic.StoreInt64(shm.read_idx, currWrite)

		// Invoke the hot path
		_ = shm.Enqueue(mockPacket)
	}
}

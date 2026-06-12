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
	shm.ingress_read_idx = (*int64)(unsafe.Pointer(&backingBuffer[0]))
	shm.ingress_write_idx = (*int64)(unsafe.Pointer(&backingBuffer[8]))
	shm.egress_read_idx = (*int64)(unsafe.Pointer(&backingBuffer[16]))
	shm.egress_write_idx = (*int64)(unsafe.Pointer(&backingBuffer[24]))

	shm.ingress_buffer = uintptr(unsafe.Pointer(&backingBuffer[32]))
	shm.egress_buffer = uintptr(unsafe.Pointer(&backingBuffer[32+ingressPacketsSize]))

	// 2. Artificially reset our queue pointers to absolute zero using atomic primitives
	atomic.StoreInt64(shm.ingress_read_idx, 0)
	atomic.StoreInt64(shm.ingress_write_idx, 0)

	// Create a generic mock packet to stream into the slots
	mockPacket := IngressPacket{
		Type: 1, Side: 2, InstrumentID: 100, TraderID: 50,
		Quantity: 10, Price: 50000, ClientOrderID: 12345,
	}

	// 3. Loop exactly 65536 times to fill every single slot in the ring buffer array
	for i := 0; i < int(SIZE); i++ {
		success := shm.Enqueue(mockPacket)
		if !success {
			t.Fatalf("Queue failed prematurely at index %d; expected true but got false", i)
		}
	}

	// 4. Attempt a 65537th Enqueue and assert that it handles boundary saturation correctly
	overfillSuccess := shm.Enqueue(mockPacket)
	if overfillSuccess {
		t.Errorf("Security boundary breached! Expected Enqueue to return false when ring buffer is completely full, but got true")
	}
}

// TestDequeueLifecycle verifies sequential parsing and empty-state bounds
func TestDequeueLifecycle(t *testing.T) {
	backingBuffer := make([]byte, totalSize)

	var shm SharedMemory
	shm.ingress_read_idx = (*int64)(unsafe.Pointer(&backingBuffer[0]))
	shm.ingress_write_idx = (*int64)(unsafe.Pointer(&backingBuffer[8]))
	shm.egress_read_idx = (*int64)(unsafe.Pointer(&backingBuffer[16]))
	shm.egress_write_idx = (*int64)(unsafe.Pointer(&backingBuffer[24]))

	shm.ingress_buffer = uintptr(unsafe.Pointer(&backingBuffer[32]))
	shm.egress_buffer = uintptr(unsafe.Pointer(&backingBuffer[32+ingressPacketsSize]))

	atomic.StoreInt64(shm.egress_read_idx, 0)
	atomic.StoreInt64(shm.egress_write_idx, 0)

	// 1. Assert Dequeue returns false immediately when ring is dry
	_, initialHasData := shm.Dequeue()
	if initialHasData {
		t.Fatalf("Expected Dequeue to return false on unpopulated empty memory canvas, got true")
	}

	// 2. Construct an outbound execution report matching your cache-aligned struct bounds
	mockEgress := EgressPacket{
		Type: 4, Side: 1, InstrumentID: 7, ClientOrderID: 888999,
		MakerID: 101, TakerID: 202, MatchPrice: 1550, MatchQuantity: 10,
	}

	// Simulating C++ Core thread behavior: Write directly to the segment slot
	slotIndex := int64(0)
	targetAddr := shm.egress_buffer + uintptr(slotIndex)*unsafe.Sizeof(EgressPacket{})
	packetSlot := (*EgressPacket)(unsafe.Pointer(targetAddr))
	*packetSlot = mockEgress
	atomic.StoreInt64(shm.egress_write_idx, 1)

	// 3. Dequeue and verify bitwise fields
	poppedPacket, hasData := shm.Dequeue()
	if !hasData {
		t.Fatalf("Expected Dequeue to locate valid execution reports, but got false")
	}

	if poppedPacket.ClientOrderID != 888999 || poppedPacket.MatchPrice != 1550 || poppedPacket.MatchQuantity != 10 {
		t.Errorf("Data corruption detected across memory alignment structures! Unpacked details mismatched.")
	}

	// 4. Invariant: Cursors advanced, ring should report empty once more
	_, finalHasData := shm.Dequeue()
	if finalHasData {
		t.Errorf("FIFO head cursor failed to lock state boundary; duplicate data popped.")
	}
}

func BenchmarkEnqueue(b *testing.B) {
	// 1. Setup isolated, in-memory shm and an IngressPacket instance
	backingBuffer := make([]byte, totalSize)

	var shm SharedMemory
	shm.ingress_read_idx = (*int64)(unsafe.Pointer(&backingBuffer[0]))
	shm.ingress_write_idx = (*int64)(unsafe.Pointer(&backingBuffer[8]))
	shm.egress_read_idx = (*int64)(unsafe.Pointer(&backingBuffer[16]))
	shm.egress_write_idx = (*int64)(unsafe.Pointer(&backingBuffer[24]))

	shm.ingress_buffer = uintptr(unsafe.Pointer(&backingBuffer[32]))
	shm.egress_buffer = uintptr(unsafe.Pointer(&backingBuffer[32+ingressPacketsSize]))

	mockPacket := IngressPacket{
		Type: 1, Side: 2, InstrumentID: 100, TraderID: 50,
		Quantity: 10, Price: 50000, ClientOrderID: 12345,
	}

	// Reset indices cleanly before beginning the test
	atomic.StoreInt64(shm.ingress_read_idx, 0)
	atomic.StoreInt64(shm.ingress_write_idx, 0)

	b.ResetTimer()

	// 2. Loop exactly b.N times
	for i := 0; i < b.N; i++ {
		// 3. Keep the queue clear by dynamically sliding the read index
		// right behind the current write index to bypass the full guard.
		currWrite := atomic.LoadInt64(shm.ingress_write_idx)
		atomic.StoreInt64(shm.ingress_read_idx, currWrite)

		// Invoke the hot path
		_ = shm.Enqueue(mockPacket)
	}
}

// BenchmarkDequeue measures micro-overhead profile of outbound processing runs
func BenchmarkDequeue(b *testing.B) {
	backingBuffer := make([]byte, totalSize)

	var shm SharedMemory
	shm.ingress_read_idx = (*int64)(unsafe.Pointer(&backingBuffer[0]))
	shm.ingress_write_idx = (*int64)(unsafe.Pointer(&backingBuffer[8]))
	shm.egress_read_idx = (*int64)(unsafe.Pointer(&backingBuffer[16]))
	shm.egress_write_idx = (*int64)(unsafe.Pointer(&backingBuffer[24]))

	shm.ingress_buffer = uintptr(unsafe.Pointer(&backingBuffer[32]))
	shm.egress_buffer = uintptr(unsafe.Pointer(&backingBuffer[32+ingressPacketsSize]))

	mockEgress := EgressPacket{
		Type: 4, Side: 1, InstrumentID: 1, ClientOrderID: 101,
		MakerID: 1001, TakerID: 2002, MatchPrice: 5000, MatchQuantity: 5,
	}

	atomic.StoreInt64(shm.egress_read_idx, 0)
	atomic.StoreInt64(shm.egress_write_idx, 0)

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		// Keep data ahead of the consumer cursor by sliding the write marker forward
		currRead := atomic.LoadInt64(shm.egress_read_idx)
		slotIndex := currRead & (SIZE - 1)

		targetAddr := shm.egress_buffer + uintptr(slotIndex)*unsafe.Sizeof(EgressPacket{})
		packetSlot := (*EgressPacket)(unsafe.Pointer(targetAddr))
		*packetSlot = mockEgress

		atomic.StoreInt64(shm.egress_write_idx, currRead+1)

		// Fire hot path execution read pass
		_, _ = shm.Dequeue()
	}
}

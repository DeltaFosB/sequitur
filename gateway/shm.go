package main

import (
	"fmt"
	"sync/atomic"
	"unsafe"

	"golang.org/x/sys/unix"
)

var (
	SIZE               int64 = 65536
	ingressPacketsSize       = int(SIZE) * int(unsafe.Sizeof(IngressPacket{}))
	egressPacketsSize        = int(SIZE) * int(unsafe.Sizeof(EgressPacket{}))
	totalSize          int   = 8 + 8 + 8 + 8 + ingressPacketsSize + egressPacketsSize
)

type SharedMemory struct {
	ingress_read_idx  *int64
	ingress_write_idx *int64
	egress_read_idx   *int64
	egress_write_idx  *int64
	ingress_buffer    uintptr
	egress_buffer     uintptr
}

func (shm *SharedMemory) InitSharedMemory() {
	fd, err := unix.Open("/dev/shm/sequitur_shm", unix.O_RDWR|unix.O_CREAT, 0o666)
	if err != nil {
		fmt.Println("Error opening shm file: ", err)
	}

	err = unix.Ftruncate(fd, int64(totalSize))
	if err != nil {
		fmt.Println("Error resizing shm file: ", err)
		return
	}

	data, err := unix.Mmap(fd, 0, totalSize, unix.PROT_READ|unix.PROT_WRITE, unix.MAP_SHARED)
	if err != nil {
		fmt.Println("Error mapping file to virtual memory: ", err)
		return
	}

	unix.Close(fd)

	shm.ingress_read_idx = (*int64)(unsafe.Pointer(&data[0]))
	shm.ingress_write_idx = (*int64)(unsafe.Pointer(&data[8]))
	shm.egress_read_idx = (*int64)(unsafe.Pointer(&data[16]))
	shm.egress_write_idx = (*int64)(unsafe.Pointer(&data[24]))

	shm.ingress_buffer = uintptr(unsafe.Pointer(&data[32]))
	shm.egress_buffer = uintptr(unsafe.Pointer(&data[32+ingressPacketsSize]))
}

func (shm *SharedMemory) Enqueue(packet IngressPacket) bool {
	currRead := atomic.LoadInt64(shm.ingress_read_idx)
	currWrite := atomic.LoadInt64(shm.ingress_write_idx)

	diff := currWrite - currRead

	if diff == int64(SIZE) {
		return false
	}

	slotIndex := currWrite & (SIZE - 1)
	targetAddr := shm.ingress_buffer + uintptr(slotIndex)*unsafe.Sizeof(IngressPacket{})
	packetSlot := (*IngressPacket)(unsafe.Pointer(targetAddr))
	*packetSlot = packet

	atomic.StoreInt64(shm.ingress_write_idx, currWrite+1)

	return true
}

func (shm *SharedMemory) Dequeue() (EgressPacket, bool) {
	currRead := atomic.LoadInt64(shm.egress_read_idx)
	currWrite := atomic.LoadInt64(shm.egress_write_idx)

	if currRead == currWrite {
		return EgressPacket{}, false
	}

	slotIndex := currRead & (SIZE - 1)
	targetAddr := shm.egress_buffer + uintptr(slotIndex)*unsafe.Sizeof(EgressPacket{})
	packetSlot := (*EgressPacket)(unsafe.Pointer(targetAddr))
	packet := *packetSlot

	atomic.StoreInt64(shm.egress_read_idx, currRead+1)
	return packet, true
}

package main

import (
	"fmt"
	"sync/atomic"
	"unsafe"

	"golang.org/x/sys/unix"
)

var (
	SIZE      int64 = 65536
	totalSize int   = 8 + 8 + int(SIZE)*int(unsafe.Sizeof(IngressPacket{}))
)

type SharedMemory struct {
	read_idx  *int64
	write_idx *int64
	buffer    uintptr
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

	shm.read_idx = (*int64)(unsafe.Pointer(&data[0]))
	shm.write_idx = (*int64)(unsafe.Pointer(&data[8]))
	shm.buffer = uintptr(unsafe.Pointer(&data[16]))
}

func (shm *SharedMemory) Enqueue(packet IngressPacket) bool {
	currRead := atomic.LoadInt64(shm.read_idx)
	currWrite := atomic.LoadInt64(shm.write_idx)

	diff := currWrite - currRead

	if diff == int64(SIZE) {
		return false
	}

	slotIndex := currWrite & (SIZE - 1)
	targetAddr := shm.buffer + uintptr(slotIndex)*unsafe.Sizeof(IngressPacket{})
	packetSlot := (*IngressPacket)(unsafe.Pointer(targetAddr))
	*packetSlot = packet

	atomic.StoreInt64(shm.write_idx, currWrite+1)

	return true
}

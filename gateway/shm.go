package main

import (
	"fmt"
	"unsafe"

	"golang.org/x/sys/unix"
)

var (
	SIZE      int = 1024
	totalSize int = 8 + 8 + SIZE*int(unsafe.Sizeof(IngressPacket{}))
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

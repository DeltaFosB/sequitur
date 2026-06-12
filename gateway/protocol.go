package main

import (
	"fmt"
	"strconv"
	"strings"
)

type IngressPacket struct {
	Type          uint8  // 1 Byte
	Side          uint8  // 1 Byte
	Padding16     uint16 // 2 Bytes (Maintains C++ structure layout compatibility)
	InstrumentID  uint32 // 4 Bytes
	TraderID      uint32 // 4 Bytes
	Quantity      uint32 // 4 Bytes
	Price         uint64 // 8 Bytes
	ClientOrderID uint64 // 8 Bytes
}

type EgressPacket struct {
	Type           uint8    // Offset 0  | 1 Byte
	Side           uint8    // Offset 1  | 1 Byte
	Padding16      uint16   // Offset 2  | 2 Bytes
	InstrumentID   uint32   // Offset 4  | 4 Bytes
	ClientOrderID  uint64   // Offset 8  | 8 Bytes
	MakerID        uint64   // Offset 16 | 8 Bytes
	TakerID        uint64   // Offset 24 | 8 Bytes
	MatchPrice     uint64   // Offset 32 | 8 Bytes
	MatchQuantity  uint32   // Offset 40 | 4 Bytes
	LeavesQuantity uint32   // Offset 44 | 4 Bytes
	Padding        [16]byte // Offset 48 | 16 Bytes -> Ends at exactly 64 Bytes
}

func (packet *IngressPacket) assignField(counter int, val string) error {
	switch counter {
	case 0:
		parsed, err := strconv.ParseUint(val, 10, 8)
		if err != nil {
			return err
		}
		packet.Type = uint8(parsed)
	case 1:
		parsed, err := strconv.ParseUint(val, 10, 8)
		if err != nil {
			return err
		}
		packet.Side = uint8(parsed)
	case 2:
		parsed, err := strconv.ParseUint(val, 10, 32)
		if err != nil {
			return err
		}
		packet.InstrumentID = uint32(parsed)
	case 3:
		parsed, err := strconv.ParseUint(val, 10, 32)
		if err != nil {
			return err
		}
		packet.TraderID = uint32(parsed)
	case 4:
		parsed, err := strconv.ParseUint(val, 10, 32)
		if err != nil {
			return err
		}
		packet.Quantity = uint32(parsed)
	case 5:
		parsed, err := strconv.ParseUint(val, 10, 64)
		if err != nil {
			return err
		}
		packet.Price = parsed
	case 6:
		parsed, err := strconv.ParseUint(val, 10, 64)
		if err != nil {
			return err
		}
		packet.ClientOrderID = parsed
	}

	return nil
}

func (packet *IngressPacket) ParseCSV(line string) error {
	line = strings.TrimSpace(line)
	left, right, counter := 0, 0, 0
	for {
		if right >= len(line) {
			break
		}
		for {
			if right >= len(line) {
				break
			}

			if line[right] == ',' {
				break
			}
			right++
		}

		val := line[left:right]

		err := packet.assignField(counter, val)
		if err != nil {
			return err
		}

		right++
		left = right
		counter++

	}
	if counter != 7 {
		return fmt.Errorf("incomplete packet: expected 7 fields, got %d", counter)
	}
	return nil
}

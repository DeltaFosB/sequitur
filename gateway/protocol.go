package main

import (
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
	return nil
}

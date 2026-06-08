package main

import (
	"testing"
	"unsafe"
)

func TestStructAlignment(t *testing.T) {
	var p IngressPacket

	// 1. Assert overall data slot layout is exactly 32 bytes
	if unsafe.Sizeof(p) != 32 {
		t.Fatalf("IngressPacket overall size mismatch: expected 32 bytes, got %d", unsafe.Sizeof(p))
	}

	// 2. Assert precise byte boundaries inside the struct frame
	if unsafe.Offsetof(p.Type) != 0 {
		t.Errorf("Type offset mismatch: expected 0, got %d", unsafe.Offsetof(p.Type))
	}
	if unsafe.Offsetof(p.Side) != 1 {
		t.Errorf("Side offset mismatch: expected 1, got %d", unsafe.Offsetof(p.Side))
	}
	if unsafe.Offsetof(p.Padding16) != 2 {
		t.Errorf("Padding16 offset mismatch: expected 2, got %d", unsafe.Offsetof(p.Padding16))
	}
	if unsafe.Offsetof(p.InstrumentID) != 4 {
		t.Errorf("InstrumentID offset mismatch: expected 4, got %d", unsafe.Offsetof(p.InstrumentID))
	}
	if unsafe.Offsetof(p.TraderID) != 8 {
		t.Errorf("TraderID offset mismatch: expected 8, got %d", unsafe.Offsetof(p.TraderID))
	}
	if unsafe.Offsetof(p.Quantity) != 12 {
		t.Errorf("Quantity offset mismatch: expected 12, got %d", unsafe.Offsetof(p.Quantity))
	}
	if unsafe.Offsetof(p.Price) != 16 {
		t.Errorf("Price offset mismatch: expected 16, got %d", unsafe.Offsetof(p.Price))
	}
	if unsafe.Offsetof(p.ClientOrderID) != 24 {
		t.Errorf("ClientOrderID offset mismatch: expected 24, got %d", unsafe.Offsetof(p.ClientOrderID))
	}
}

func TestParseCSV_Matrix(t *testing.T) {
	testCases := []struct {
		name          string
		input         string
		expectErr     bool
		expectedField IngressPacket // Expected struct baseline if expectErr is false
	}{
		{
			name:      "HappyPathShort",
			input:     "1,2,1005,5002,10,150450,999101\n",
			expectErr: false,
			expectedField: IngressPacket{
				Type: 1, Side: 2, InstrumentID: 1005, TraderID: 5002,
				Quantity: 10, Price: 150450, ClientOrderID: 999101,
			},
		},
		{
			name:      "MaxNumericFields",
			input:     "255,255,4294967295,4294967295,4294967295,18446744073709551615,18446744073709551615\n",
			expectErr: false,
			expectedField: IngressPacket{
				Type: 255, Side: 255, InstrumentID: 4294967295, TraderID: 4294967295,
				Quantity: 4294967295, Price: 18446744073709551615, ClientOrderID: 18446744073709551615,
			},
		},
		{
			name:      "TrailingCarriageReturn",
			input:     "1,2,1005,5002,10,150450,999101\r\n",
			expectErr: false,
			expectedField: IngressPacket{
				Type: 1, Side: 2, InstrumentID: 1005, TraderID: 5002,
				Quantity: 10, Price: 150450, ClientOrderID: 999101,
			},
		},
		{
			name:      "MalformedAlphaCharacters",
			input:     "1,2,INVALID_ID,5002,10,150450,999101\n",
			expectErr: true,
		},
		{
			name:      "MissingFields",
			input:     "1,2,1005\n",
			expectErr: true,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			var packet IngressPacket
			err := packet.ParseCSV(tc.input)

			// 1. Evaluate error expectations
			if tc.expectErr {
				if err == nil {
					t.Fatalf("Expected parsing error but got nil")
				}
				return // Test case passed successfully, drop out early
			}

			if err != nil {
				t.Fatalf("Unexpected parsing error: %v", err)
			}

			// 2. Evaluate field correctness bit-by-bit
			if packet.Type != tc.expectedField.Type {
				t.Errorf("Type mismatch: expected %d, got %d", tc.expectedField.Type, packet.Type)
			}
			if packet.Side != tc.expectedField.Side {
				t.Errorf("Side mismatch: expected %d, got %d", tc.expectedField.Side, packet.Side)
			}
			if packet.InstrumentID != tc.expectedField.InstrumentID {
				t.Errorf("InstrumentID mismatch: expected %d, got %d", tc.expectedField.InstrumentID, packet.InstrumentID)
			}
			if packet.TraderID != tc.expectedField.TraderID {
				t.Errorf("TraderID mismatch: expected %d, got %d", tc.expectedField.TraderID, packet.TraderID)
			}
			if packet.Quantity != tc.expectedField.Quantity {
				t.Errorf("Quantity mismatch: expected %d, got %d", tc.expectedField.Quantity, packet.Quantity)
			}
			if packet.Price != tc.expectedField.Price {
				t.Errorf("Price mismatch: expected %d, got %d", tc.expectedField.Price, packet.Price)
			}
			if packet.ClientOrderID != tc.expectedField.ClientOrderID {
				t.Errorf("ClientOrderID mismatch: expected %d, got %d", tc.expectedField.ClientOrderID, packet.ClientOrderID)
			}
		})
	}
}

// BenchmarkParseCSV measures the precise execution speed and heap
// memory allocation footprint of the CSV processing hot path across
// varied field lengths and whitespace scenarios.
func BenchmarkParseCSV(b *testing.B) {
	benchmarks := []struct {
		name  string
		input string
	}{
		{
			name:  "HappyPathShort",
			input: "1,2,1005,5002,10,150450,999101\n",
		},
		{
			name:  "MaxNumericFields",
			input: "255,255,4294967295,4294967295,4294967295,18446744073709551615,18446744073709551615\n",
		},
		{
			name:  "TrailingCarriageReturn",
			input: "1,2,1005,5002,10,150450,999101\r\n",
		},
	}

	for _, bm := range benchmarks {
		b.Run(bm.name, func(b *testing.B) {
			var packet IngressPacket
			b.ResetTimer()

			for i := 0; i < b.N; i++ {
				_ = packet.ParseCSV(bm.input)
			}
		})
	}
}

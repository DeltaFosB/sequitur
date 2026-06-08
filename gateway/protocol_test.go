package main

import (
	"testing"
)

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

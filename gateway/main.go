package main

import (
	"bufio"
	"fmt"
	"net"
	"sync"
)

var (
	clientRegistry = make(map[uint32]net.Conn)
	registryMutex  sync.RWMutex
)

func HandleClient(conn net.Conn, packetChan chan<- IngressPacket) {
	defer conn.Close()
	reader := bufio.NewReader(conn)
	var assignedTraderID uint32 = 0
	var registered bool

	for {
		message, err := reader.ReadString('\n')
		if err != nil {
			if registered {
				registryMutex.Lock()
				delete(clientRegistry, assignedTraderID)
				registryMutex.Unlock()
			}
			fmt.Println("Client Disconnected.")
			return
		}
		// fmt.Println("Received: ", message)

		var packet IngressPacket
		err = packet.ParseCSV(message)
		if err != nil {
			fmt.Println("Parsing error: ", err)
			continue
		}

		if !registered {
			assignedTraderID = packet.TraderID
			registryMutex.Lock()
			clientRegistry[assignedTraderID] = conn
			registryMutex.Unlock()
			registered = true
		}

		packetChan <- packet
	}
}

func ServeGateway(listener net.Listener, shm *SharedMemory, packetChan chan IngressPacket) {
	// 1. Ingress Processing Engine Thread Loop
	go func() {
		for packet := range packetChan {
			if success := shm.Enqueue(packet); !success {
				fmt.Println("Shared memory ring buffer full! Drop or backpressure triggered.")
			}
		}
	}()

	// 2. NEW: Egress Distribution Engine Thread Loop
	go func() {
		for {
			packet, hasData := shm.Dequeue()
			if !hasData {
				// Ring buffer empty; let the thread cooperatively yield
				// Using a tight spin override option for sub-microsecond delivery
				continue
			}

			reportStr := fmt.Sprintf("%d,%d,%d,%d,%d,%d\n",
				packet.Type, packet.Side, packet.InstrumentID,
				packet.ClientOrderID, packet.MatchPrice, packet.MatchQuantity)
			payload := []byte(reportStr)

			// Route to the Aggressive Taker if they are connected to this gateway instance
			if takerConn, exists := clientRegistry[uint32(packet.TakerID)]; exists {
				_, _ = takerConn.Write(payload)
			}

			// Route to the Passive Maker if they are connected to this gateway instance
			if makerConn, exists := clientRegistry[uint32(packet.MakerID)]; exists {
				_, _ = makerConn.Write(payload)
			}

		}
	}()

	fmt.Println("Sequitur Go Gateway Engine Activated.")
	for {
		conn, err := listener.Accept()
		if err != nil {
			return
		}
		fmt.Println("New market client connected!")

		go HandleClient(conn, packetChan)
	}
}

func main() {
	var shm SharedMemory
	shm.InitSharedMemory()

	packetChan := make(chan IngressPacket, 1024)

	listener, err := net.Listen("tcp", ":8080")
	if err != nil {
		fmt.Println("Socket creation failed:", err)
		return
	}
	defer listener.Close()

	fmt.Println("Sequitur Go Gateway Initialized on port :8080.")

	ServeGateway(listener, &shm, packetChan)
}

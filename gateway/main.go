package main

import (
	"bufio"
	"fmt"
	"net"
	"sync"
)

type clientState struct {
	conn net.Conn
	ch   chan []byte
}

var (
	clientRegistry = make(map[uint32]*clientState)
	registryMutex  sync.RWMutex
)

func HandleClient(conn net.Conn, packetChan chan<- IngressPacket) {
	reader := bufio.NewReader(conn)

	localSessionTraders := make(map[uint32]struct{})

	state := &clientState{
		conn: conn,
		ch:   make(chan []byte, 65536),
	}

	go func(cs *clientState) {
		for payload := range cs.ch {
			_, err := cs.conn.Write(payload)
			if err != nil {
				return
			}
		}
	}(state)

	defer func() {
		conn.Close()
		registryMutex.Lock()

		for traderID := range localSessionTraders {
			delete(clientRegistry, traderID)
		}
		close(state.ch)

		registryMutex.Unlock()
		fmt.Printf("Connection terminated. Unregistered bound traders: %v\n", localSessionTraders)
	}()

	for {
		message, err := reader.ReadString('\n')
		if err != nil {
			return
		}

		var packet IngressPacket
		err = packet.ParseCSV(message)
		if err != nil {
			fmt.Println("Parsing error: ", err)
			continue
		}

		traderID := packet.TraderID
		if _, exists := localSessionTraders[traderID]; !exists {
			localSessionTraders[traderID] = struct{}{}

			registryMutex.Lock()
			clientRegistry[traderID] = state
			registryMutex.Unlock()

			fmt.Printf("Trader %d dynamically registered and bound to active session.\n", traderID)
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

	// 2. Egress Distribution Engine Thread Loop (Fully Decoupled)
	go func() {
		for {
			packet, hasData := shm.Dequeue()
			if !hasData {
				// Ring buffer empty; let the thread cooperatively yield
				continue
			}

			reportStr := fmt.Sprintf("%d,%d,%d,%d,%d,%d\n",
				packet.Type, packet.Side, packet.InstrumentID,
				packet.ClientOrderID, packet.MatchPrice, packet.MatchQuantity)
			payload := []byte(reportStr)

			registryMutex.RLock()

			// Route to the Aggressive Taker asynchronously via non-blocking select channel drops
			if takerState, exists := clientRegistry[uint32(packet.TakerID)]; exists {
				select {
				case takerState.ch <- payload:
				default:
					// Prevents a bottlenecked client socket from stalling the global pipeline
				}
			}

			// Route to the Passive Maker asynchronously via non-blocking select channel drops
			if makerState, exists := clientRegistry[uint32(packet.MakerID)]; exists {
				select {
				case makerState.ch <- payload:
				default:
				}
			}

			registryMutex.RUnlock()
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

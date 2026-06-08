package main

import (
	"bufio"
	"fmt"
	"net"
)

func HandleClient(conn net.Conn, packetChan chan<- IngressPacket) {
	defer conn.Close()
	reader := bufio.NewReader(conn)
	for {
		message, err := reader.ReadString('\n')
		if err != nil {
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

		packetChan <- packet
	}
}

func ServeGateway(listener net.Listener, shm *SharedMemory, packetChan chan IngressPacket) {
	go func() {
		for packet := range packetChan {
			if success := shm.Enqueue(packet); !success {
				fmt.Println("Shared memory ring buffer full! Drop or backpressure triggered.")
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

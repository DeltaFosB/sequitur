package main

import (
	"bufio"
	"fmt"
	"net"
)

func HandleClient(conn net.Conn) {
	defer conn.Close()
	reader := bufio.NewReader(conn)
	for {
		message, err := reader.ReadString('\n')
		if err != nil {
			fmt.Println("Client Disconnected.")
			return
		}
		fmt.Println("Recieved: ", message)
		var packet IngressPacket
		err = packet.ParseCSV(message)
		if err != nil {
			fmt.Println("Parsing error: ", err)
		}
		continue
	}
}

func main() {
	listener, err := net.Listen("tcp", ":8080")
	if err != nil {
		fmt.Println("Socket creation failed.")
		return
	}
	fmt.Println("Sequitur Go Gatway Initialized.")
	for {
		conn, err := listener.Accept()
		if err != nil {
			continue
		}
		fmt.Println("New market client connected!")

		go HandleClient(conn)
	}
}

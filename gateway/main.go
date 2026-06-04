package main

import (
	"fmt"
	"net"
)

func HandleClient(conn net.Conn) {
	// Client processing logic goes here
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

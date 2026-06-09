#!/usr/bin/env python3
import socket
import sys
import time


def stream_market_traffic(host="127.0.0.1", port=8080, batch_size=1000):
    """
    Incessantly streams unique formatted CSV order packets down a persistent TCP socket.
    Dynamically increments ClientOrderIDs to satisfy deduplication rules and drive
    immediate order book crossing and trade generation.
    """
    print(f"[Client] Initializing connection to Go Gateway at {host}:{port}...")

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.connect((host, port))
        print("[Client] Connection established. Active market simulation online.")
        print("[Client] Injection active loop engaged. Press [Ctrl+C] to halt.")
    except Exception as e:
        print(f"[Client Fatal] Connection failed: {e}")
        sys.exit(1)

    start_time = time.time()
    sent_count = 0
    packet_id = 1  # Initialize our unique monotonic sequence tracker

    try:
        while True:
            payload_buffer = bytearray()
            for _ in range(batch_size):
                # Dynamically construct buy and sell matching pairs with unique IDs
                buy_str = f"1,0,1,101,10,1000,{packet_id}\n"
                sell_str = f"1,1,1,102,10,1000,{packet_id + 1}\n"

                payload_buffer.extend(buy_str.encode("utf-8"))
                payload_buffer.extend(sell_str.encode("utf-8"))

                # Advance the counter to maintain total uniqueness
                packet_id += 2

            sock.sendall(payload_buffer)
            sent_count += batch_size * 2
            time.sleep(0.001)

    except KeyboardInterrupt:
        print(
            "\n[Client] Traffic injection halted gracefully via user shutdown signal."
        )
    except Exception as e:
        print(f"\n[Client] Network write error: {e}")
    finally:
        sock.close()
        duration = time.time() - start_time
        print("----------------------------------------------------------------")
        print(f"[Client Summary] Active Stream Time : {duration:.2f} seconds.")
        print(f"[Client Summary] Total Packets Injected: {sent_count}")
        if duration > 0:
            print(
                f"[Client Summary] Sustained Velocity    : {sent_count / duration:.2f} OPS"
            )
        print("----------------------------------------------------------------")


if __name__ == "__main__":
    stream_market_traffic()

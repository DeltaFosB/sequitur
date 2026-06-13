#!/usr/bin/env python3
import socket
import sys
import threading
import time


def stream_market_traffic(host="127.0.0.1", port=8080, batch_size=1000):
    print(f"[Client] Initializing connection to Go Gateway at {host}:{port}...")

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.connect((host, port))
        print("[Client] Connection established. Active market simulation online.")
    except Exception as e:
        print(f"[Client Fatal] Connection failed: {e}")
        sys.exit(1)

    sent_count = 0
    accepted_count = 0
    filled_count = 0
    rejected_count = 0

    packet_id = 1
    shutdown_event = threading.Event()
    stats_lock = threading.Lock()

    def egress_reader_loop():
        nonlocal accepted_count, filled_count, rejected_count
        socket_buffer = ""

        try:
            while not shutdown_event.is_set():
                data_chunk = sock.recv(4096).decode("utf-8")
                if not data_chunk:
                    break

                socket_buffer += data_chunk

                while "\n" in socket_buffer:
                    line, socket_buffer = socket_buffer.split("\n", 1)
                    if not line.strip():
                        continue

                    fields = line.split(",")
                    if len(fields) != 6:
                        continue

                    msg_type = int(fields[0])

                    with stats_lock:
                        if msg_type == 1:
                            accepted_count += 1
                        elif msg_type == 4:
                            filled_count += 1
                        elif msg_type == 2:
                            rejected_count += 1

        except Exception as e:
            if not shutdown_event.is_set():
                print(f"[Egress Thread Exception] Read error: {e}")

    reader_thread = threading.Thread(target=egress_reader_loop, daemon=True)
    reader_thread.start()

    print(
        "[Client] Injection and real-time verification loops engaged. Press [Ctrl+C] to halt."
    )
    start_time = time.time()

    try:
        while True:
            payload_buffer = bytearray()
            for _ in range(batch_size):
                buy_str = f"1,0,1,101,10,1000,{packet_id}\n"
                sell_str = f"1,1,1,102,10,1000,{packet_id + 1}\n"

                payload_buffer.extend(buy_str.encode("utf-8"))
                payload_buffer.extend(sell_str.encode("utf-8"))
                packet_id += 2

            sock.sendall(payload_buffer)

            with stats_lock:
                sent_count += batch_size * 2

            time.sleep(0.001)

    except KeyboardInterrupt:
        print(
            "\n[Client] Traffic injection halted gracefully via user shutdown signal."
        )
    except Exception as e:
        print(f"\n[Client] Network write error: {e}")
    finally:
        shutdown_event.set()
        try:
            sock.shutdown(socket.SHUT_WR)
        except Exception:
            pass

        reader_thread.join(timeout=2.0)
        sock.close()

        duration = time.time() - start_time
        total_responses = accepted_count + filled_count + rejected_count

        print("----------------------------------------------------------------")
        print(f"[Client Summary] Active Stream Time    : {duration:.2f} seconds.")
        print(f"[Client Summary] Total Orders Sent     : {sent_count}")
        print(f"[Client Summary] Total Frames Recv     : {total_responses}")
        print(f"  |- Order Accepted Messages  : {accepted_count}")
        print(f"  |- Order Filled Trade Execs : {filled_count}")
        print(f"  |- Order Rejected Failures  : {rejected_count}")
        if duration > 0:
            print(
                f"[Client Summary] Sustained Velocity    : {sent_count / duration:.2f} OPS"
            )

        with stats_lock:
            completed_trades = filled_count / 2
            processed_balance = accepted_count + completed_trades + rejected_count

        print(
            f"[Client Balance Audit] Total Settled Lifecycle States: {processed_balance:.1f}"
        )

        if sent_count >= processed_balance:
            print(
                f"[Client Invariant Check] SUCCESS: Platform stable. Open/In-Flight orders resting in book: {sent_count - processed_balance:.0f}"
            )
        else:
            print(
                f"[Client Invariant Check] CRITICAL ERROR: Inbound frame inflation delta: {sent_count - processed_balance:.1f}"
            )
        print("----------------------------------------------------------------")


if __name__ == "__main__":
    stream_market_traffic()

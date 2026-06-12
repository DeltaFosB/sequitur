#!/usr/bin/env python3
import socket
import sys
import threading
import time


def stream_market_traffic(host="127.0.0.1", port=8080, batch_size=1000):
    """
    Streams unique formatted CSV order packets down a persistent TCP socket
    and tracks granular outbound versus inbound message states by transaction types.
    """
    print(f"[Client] Initializing connection to Go Gateway at {host}:{port}...")

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.connect((host, port))
        print("[Client] Connection established. Active market simulation online.")
    except Exception as e:
        print(f"[Client Fatal] Connection failed: {e}")
        sys.exit(1)

    # Granular Message Audit States
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

                    # Extract the EgressType field enum byte value (Field 0)
                    msg_type = int(fields[0])

                    with stats_lock:
                        if msg_type == 1:  # EgressType::ORDER_ACCEPTED
                            accepted_count += 1
                        elif msg_type == 4:  # EgressType::ORDER_FILLED
                            filled_count += 1
                        elif msg_type == 2:  # EgressType::ORDER_REJECTED
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

        # Invariant Validation:
        # 1. Every resting order generates 1 ACCEPTED frame.
        # 2. Every crossing match sends 2 FILLED frames to this multiplexed socket (1 Maker, 1 Taker).
        with stats_lock:
            completed_trades = filled_count / 2
            processed_balance = accepted_count + completed_trades + rejected_count

        print(
            f"[Client Balance Audit] Total Settled Lifecycle States: {processed_balance:.1f}"
        )

        # In an early exit (Ctrl+C), Sent Orders will be higher than Settled States
        # because aggressive crossing orders are still sitting unmatched or trapped in transit buffers.
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

import os
import socket
import threading
from datetime import datetime

HOST = "0.0.0.0"
PORT = int(os.environ.get("TCP_PORT", "5000"))

def log(msg: str) -> None:
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"[A][{now}] {msg}", flush=True)

def handle_client(conn: socket.socket, addr):
    log(f"Accepted connection from {addr[0]}:{addr[1]}")
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                log(f"Client disconnected: {addr[0]}:{addr[1]}")
                break
            text = data.decode("utf-8", errors="replace").rstrip()
            log(f"Received {len(data)} bytes from {addr[0]}:{addr[1]} -> {text}")
    except Exception as e:
        log(f"Client error {addr[0]}:{addr[1]}: {e}")
    finally:
        try:
            conn.close()
        except Exception:
            pass

def main():
    log(f"Starting TCP server on {HOST}:{PORT}")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, PORT))
        s.listen()
        log("Listening for connections...")
        while True:
            conn, addr = s.accept()
            t = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
            t.start()

if __name__ == "__main__":
    main()
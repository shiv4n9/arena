import socket
import sys

def main():
    port = 12345
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', port))
    
    print(f"UDP Echo Server listening on 127.0.0.1:{port}")
    
    try:
        while True:
            data, addr = sock.recvfrom(1024)
            sock.sendto(data, addr)
    except KeyboardInterrupt:
        print("Stopping server.")

if __name__ == "__main__":
    main()

import socket

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 12345))
    print("UDP Echo Server listening on 127.0.0.1:12345")
    while True:
        data, addr = sock.recvfrom(1024)
        sock.sendto(data, addr)

if __name__ == "__main__":
    main()

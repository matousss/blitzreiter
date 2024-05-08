import socket
import time
import os

HOST = "192.168.1.237"  # The server's hostname or IP address
PORT = 8686

HEAD_SIZE = 5
HEAD = bytearray([0xC5, 0x33, 0xFC, 0x33, 0xFC])


def work():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.connect((HOST, PORT))
        except ConnectionRefusedError:
            print("Connection failed")
            return
        while True:
            s.send(HEAD)
            data = s.recv(13)

            head = data[:5]
            body = data[5:]
            if head == HEAD:
                # os.system("clear")
                print(f"Received {' '.join(f'0x{x:02x}' for x in body)}")
                x = int.from_bytes(body[:4], byteorder="little", signed=True)
                y = int.from_bytes(body[4:8], byteorder="little", signed=True)
                print(f"x = {x}, y = {y}")
            time.sleep(0.005)


while True:
    work()
    time.sleep(3)

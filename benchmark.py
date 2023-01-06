import time
import random
import socket
import argparse
from multiprocessing import Process

def start_client(port : int):
    # establish tcp conncection 
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(("localhost", port))

    # receive the welcome message
    sock.recv(1024)

    # stall for random amount of time
    time.sleep(random.random() * 5)

    # record the starting time
    start_time = time.time()

    # send a message
    sock.sendall(b"Hello, World!")

    # receive the response
    sock.recv(1024)

    # record the end time
    end_time = time.time()  

    # stall for random amount of time
    time.sleep(random.random() * 5)

    # close the connection
    sock.close()

    print(f"Connection took {end_time - start_time} seconds")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--connections", type=int, default=5)
    args = parser.parse_args()

    processes = list()
    for i in range(args.connections):
        p = Process(target=start_client, args=(args.port,))
        p.start()

    for p, start_time in processes:
        p.join()

if __name__ == "__main__":
    main()
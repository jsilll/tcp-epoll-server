import time
import random
import socket
import argparse
from multiprocessing import Process

def start_client(port: int):
    # establish tcp connection
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


if __name__ == "__main__":
    # Parse the command line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--connections", type=int, default=1)
    args = parser.parse_args()

    # Initialize the processes
    processes = [Process(target=start_client, args=(args.port,)) for _ in range(args.connections)]

    # Start the processes    
    for p in processes:
        p.start()

    # Wait for the processes to finish
    for p in processes:
        p.join()
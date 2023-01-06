#include "server.h"
#include "echo_handler.h"

#include <iostream>

#define PORT 8080
#define EVENTS 16
#define THREADS 4
#define BUF_SIZE 1024

int main() {
    EchoHandler<BUF_SIZE> handler;

    try {
        tcp::Server<EVENTS, BUF_SIZE> server(PORT, THREADS);
        std::cout << "Starting started on port: " << PORT << std::endl;
        server.Run(handler);
    } catch (const tcp::Error &e) {
        std::cerr << e.kind() << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
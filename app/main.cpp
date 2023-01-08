#include "handler.h"

#include <iostream>

#define PORT 8080
#define EVENTS 16
#define THREADS 4
#define BUFFER_SIZE 1024

int main() {
    try {
        EchoHandler handler;
        tcp::Server server(PORT, THREADS, BUFFER_SIZE, EVENTS);
        std::cout << "Starting started on port: " << PORT << std::endl;
        server.Run(handler);
    } catch (const tcp::Error &e) {
        std::cerr << e.kind() << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
#include <zmq.hpp>
#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <unistd.h>   // getpid()
#include <random>

void send_and_wait(zmq::socket_t &sock, const std::string &payload, std::string &reply) {
    zmq::message_t msg(payload.data(), payload.size());

#if defined(ZMQ_VERSION_MAJOR) && (ZMQ_VERSION_MAJOR > 4 || (ZMQ_VERSION_MAJOR == 4 && ZMQ_VERSION_MINOR >= 3))
    // Modern API (non-deprecated)
    sock.send(msg, zmq::send_flags::none);
    zmq::message_t resp;
    auto r = sock.recv(resp, zmq::recv_flags::none);
    if (!r) {
        throw std::runtime_error("recv() failed or was interrupted");
    }
    reply = std::string(static_cast<char*>(resp.data()), resp.size());
#else
    // Legacy API (older libzmq / cppzmq)
    sock.send(msg);
    zmq::message_t resp;
    sock.recv(resp);
    reply = std::string(static_cast<char*>(resp.data()), resp.size());
#endif
}

std::string random_hex(int len=6) {
    static std::mt19937_64 rng((uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(len);
    for (int i = 0; i < len; ++i) s.push_back(hex[rng() & 0xf]);
    return s;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " <resource> READ\n"
                  << "  " << argv[0] << " <resource> WRITE <data> [sleep_seconds]\n";
        return 1;
    }

    std::string resource = argv[1];
    std::string op = argv[2];
    std::string data;
    int sleep_seconds = 0;
    if (op == "WRITE") {
        if (argc < 4) {
            std::cerr << "WRITE requires data argument\n";
            return 1;
        }
        data = argv[3];
        if (argc >= 5) sleep_seconds = std::stoi(argv[4]);
    }

    zmq::context_t ctx{1};
    zmq::socket_t req{ctx, zmq::socket_type::req};

    // Set a readable identity
    std::string identity = "client-" + std::to_string(getpid()) + "-" + random_hex(4);

#if defined(ZMQ_VERSION_MAJOR) && (ZMQ_VERSION_MAJOR > 4 || (ZMQ_VERSION_MAJOR == 4 && ZMQ_VERSION_MINOR >= 7))
    // Preferred modern set API (since libzmq/cppzmq around 4.7.0)
    req.set(zmq::sockopt::identity, identity);
#else
    // Fallback to legacy setsockopt if modern set() is not available
    req.setsockopt(ZMQ_IDENTITY, identity.data(), identity.size());
#endif

    const std::string endpoint = "tcp://localhost:5555";
    std::cout << "CONNECTING to lock server at " << endpoint << " as " << identity << std::endl;
    req.connect(endpoint);

    // Request lock
    std::string lock_req = "LOCK " + resource;
    std::cout << "REQUESTING lock for resource: " << resource << std::endl;
    std::string reply;
    try {
        send_and_wait(req, lock_req, reply);
    } catch (const std::exception &e) {
        std::cerr << "Error during LOCK: " << e.what() << std::endl;
        return 1;
    }

    if (reply == "LOCK_GRANTED") {
        std::cout << "LOCKED " << resource << std::endl;
    } else {
        std::cerr << "Unexpected reply to LOCK: " << reply << std::endl;
        return 1;
    }

    if (op == "WRITE") {
        if (sleep_seconds > 0) {
            std::cout << "Sleeping for " << sleep_seconds << " seconds before WRITE..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(sleep_seconds));
        }
        std::string write_req = "WRITE " + resource + " " + data;
        std::cout << "WRITING value to " << resource << ": " << data << std::endl;
        send_and_wait(req, write_req, reply);
        std::cout << "WRITE reply: " << reply << std::endl;
    } else if (op == "READ") {
        std::string read_req = "READ " + resource;
        send_and_wait(req, read_req, reply);
        if (reply.rfind("READ_OK ", 0) == 0) {
            std::string value = reply.substr(8);
            std::cout << "READING value from " << resource << ": " << value << std::endl;
        } else {
            std::cout << "READ reply: " << reply << std::endl;
        }
    } else {
        std::cerr << "Unknown operation: " << op << std::endl;
    }

    // Release lock
    std::string unlock_req = "UNLOCK " + resource;
    std::cout << "RELEASING lock for resource: " << resource << std::endl;
    send_and_wait(req, unlock_req, reply);
    std::cout << "UNLOCK reply: " << reply << std::endl;

    return 0;
}
#include <zmq.hpp>
#include <string>
#include <iostream>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <vector>
#include <sstream>

struct Resource {
    std::string owner; // client identity string
    std::deque<std::string> queue; // queued client identities
    std::string value; // stored content for WRITE/READ
};

std::vector<std::string> split_n(const std::string &s, char delim, int max_parts) {
    std::vector<std::string> out;
    std::string cur;
    int parts = 1;
    for (char c : s) {
        if (c == delim && parts < max_parts) {
            out.push_back(cur);
            cur.clear();
            parts++;
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

int main() {
    zmq::context_t ctx{1};
    zmq::socket_t router{ctx, zmq::socket_type::router};
    const std::string endpoint = "tcp://*:5555";
    router.bind(endpoint);
    std::cout << "Lock Server started on " << endpoint << std::endl;

    std::unordered_map<std::string, Resource> resources;
    std::mutex mtx;

    while (true) {
        // ROUTER receives frames: [identity][empty][payload]
        zmq::message_t identity;
        zmq::message_t empty;
        zmq::message_t payload;

        // receive identity
        if (!router.recv(identity, zmq::recv_flags::none)) continue;
        // receive empty
        if (!router.recv(empty, zmq::recv_flags::none)) continue;
        // receive payload
        if (!router.recv(payload, zmq::recv_flags::none)) continue;

        std::string id_str(static_cast<char*>(identity.data()), identity.size());
        std::string msg(static_cast<char*>(payload.data()), payload.size());

        // Parse message: OP RESOURCE [DATA...]
        auto parts = split_n(msg, ' ', 3);
        std::string op = parts.size() > 0 ? parts[0] : "";
        std::string resource = parts.size() > 1 ? parts[1] : "";
        std::string data = parts.size() > 2 ? parts[2] : "";

        if (op == "LOCK") {
            std::lock_guard<std::mutex> lock(mtx);
            auto &res = resources[resource];
            if (res.owner.empty()) {
                // grant immediately
                res.owner = id_str;

                // send reply: [identity][empty][LOCK_GRANTED]
                zmq::message_t out_id(id_str.data(), id_str.size());
                zmq::message_t out_empty(0);
                std::string reply = "LOCK_GRANTED";
                zmq::message_t out_payload(reply.data(), reply.size());

                router.send(out_id, zmq::send_flags::sndmore);
                router.send(out_empty, zmq::send_flags::sndmore);
                router.send(out_payload, zmq::send_flags::none);

                std::cout << "GRANTED lock for resource: " << resource << " to client " << id_str << std::endl;
            } else {
                // queue the client; do NOT reply now (client REQ will block)
                resources[resource].queue.push_back(id_str);
                std::cout << "QUEUED client " << id_str << " for resource: " << resource << std::endl;
                // no reply -> client stays blocked until we grant
            }
        } else if (op == "WRITE") {
            std::lock_guard<std::mutex> lock(mtx);
            auto &res = resources[resource];
            if (res.owner == id_str) {
                res.value = data;
                std::string reply = "WRITE_OK";
                // send reply
                zmq::message_t out_id(id_str.data(), id_str.size());
                zmq::message_t out_empty(0);
                zmq::message_t out_payload(reply.data(), reply.size());
                router.send(out_id, zmq::send_flags::sndmore);
                router.send(out_empty, zmq::send_flags::sndmore);
                router.send(out_payload, zmq::send_flags::none);

                std::cout << "Client " << id_str << " wrote to " << resource << ": " << data << std::endl;
            } else {
                std::string reply = "WRITE_DENIED";
                zmq::message_t out_id(id_str.data(), id_str.size());
                zmq::message_t out_empty(0);
                zmq::message_t out_payload(reply.data(), reply.size());
                router.send(out_id, zmq::send_flags::sndmore);
                router.send(out_empty, zmq::send_flags::sndmore);
                router.send(out_payload, zmq::send_flags::none);

                std::cout << "WRITE_DENIED for client " << id_str << " on resource " << resource << std::endl;
            }
        } else if (op == "READ") {
            std::lock_guard<std::mutex> lock(mtx);
            auto &res = resources[resource];
            if (res.owner == id_str) {
                std::string reply = "READ_OK " + res.value;
                zmq::message_t out_id(id_str.data(), id_str.size());
                zmq::message_t out_empty(0);
                zmq::message_t out_payload(reply.data(), reply.size());
                router.send(out_id, zmq::send_flags::sndmore);
                router.send(out_empty, zmq::send_flags::sndmore);
                router.send(out_payload, zmq::send_flags::none);

                std::cout << "Client " << id_str << " read from " << resource << ": " << res.value << std::endl;
            } else {
                std::string reply = "READ_DENIED";
                zmq::message_t out_id(id_str.data(), id_str.size());
                zmq::message_t out_empty(0);
                zmq::message_t out_payload(reply.data(), reply.size());
                router.send(out_id, zmq::send_flags::sndmore);
                router.send(out_empty, zmq::send_flags::sndmore);
                router.send(out_payload, zmq::send_flags::none);

                std::cout << "READ_DENIED for client " << id_str << " on resource " << resource << std::endl;
            }
        } else if (op == "UNLOCK") {
            std::lock_guard<std::mutex> lock(mtx);
            auto &res = resources[resource];
            if (res.owner == id_str) {
                // reply to unlocking client
                std::string reply = "UNLOCKED";
                zmq::message_t out_id(id_str.data(), id_str.size());
                zmq::message_t out_empty(0);
                zmq::message_t out_payload(reply.data(), reply.size());
                router.send(out_id, zmq::send_flags::sndmore);
                router.send(out_empty, zmq::send_flags::sndmore);
                router.send(out_payload, zmq::send_flags::none);

                std::cout << "Client " << id_str << " unlocked resource " << resource << std::endl;

                // if queue non-empty, pop next client and grant
                if (!res.queue.empty()) {
                    std::string next_id = res.queue.front();
                    res.queue.pop_front();
                    res.owner = next_id;

                    std::string grant = "LOCK_GRANTED";
                    zmq::message_t out_next_id(next_id.data(), next_id.size());
                    zmq::message_t out_next_empty(0);
                    zmq::message_t out_next_payload(grant.data(), grant.size());

                    // send the grant to the queued client (which is still blocked on its original LOCK request)
                    router.send(out_next_id, zmq::send_flags::sndmore);
                    router.send(out_next_empty, zmq::send_flags::sndmore);
                    router.send(out_next_payload, zmq::send_flags::none);

                    std::cout << "GRANTED lock for resource: " << resource << " to queued client " << next_id << std::endl;
                } else {
                    // no waiting client
                    res.owner.clear();
                    // note: we keep value in res.value (so future locks can read it)
                }
            } else {
                // not owner
                std::string reply = "UNLOCK_DENIED";
                zmq::message_t out_id(id_str.data(), id_str.size());
                zmq::message_t out_empty(0);
                zmq::message_t out_payload(reply.data(), reply.size());
                router.send(out_id, zmq::send_flags::sndmore);
                router.send(out_empty, zmq::send_flags::sndmore);
                router.send(out_payload, zmq::send_flags::none);

                std::cout << "UNLOCK_DENIED for client " << id_str << " on resource " << resource << std::endl;
            }
        } else {
            // unknown op -> reply errorA
            std::string reply = "ERR UnknownOp";
            zmq::message_t out_id(id_str.data(), id_str.size());
            zmq::message_t out_empty(0);
            zmq::message_t out_payload(reply.data(), reply.size());
            router.send(out_id, zmq::send_flags::sndmore);
            router.send(out_empty, zmq::send_flags::sndmore);
            router.send(out_payload, zmq::send_flags::none);

            std::cout << "Unknown operation from client " << id_str << ": " << msg << std::endl;
        }
    }

    return 0;
}
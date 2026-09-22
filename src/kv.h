#ifndef KV_H
#define KV_H

#include <unordered_map>
#include <string>
#include <asio.hpp>
#include "kv.pb.h"
#include "framing.h"

using namespace crafty;
using asio::ip::tcp;

enum KVRequestType {
    GET,
    PUT,
    DEL,
};

struct KVRequest {
    KVRequestType type;
    std::string key;
    std::string value;
};

struct KVReply {
    std::string value;
    int leader;
    bool success;
};

class KVSession : public std::enable_shared_from_this<KVSession> {
    public:
        KVSession(tcp::socket socket, std::function<void(std::string, std::function<void(std::string)>)> read_callback);
        void start();
    private:
        void do_read();
        void do_write();
        tcp::socket socket_;
        std::string data_in_;
        std::string data_out_;
        std::array<unsigned char, 4> header_;
        std::function<void(std::string, std::function<void(std::string)>)> read_callback_;
};

class KVService {
    public:
        KVService(asio::io_context& io, int port, std::function<void(KVRequest, std::function<void(KVReply)>)> request_callback);
    private:
        void do_accept();
        asio::io_context& io_;
        int port_;
        std::function<void(KVRequest, std::function<void(KVReply)>)> request_callback_;
        std::function<std::string(std::string)> read_callback_;
        tcp::acceptor acceptor_;
};


class KVStore {
    public: 
        KVStore();
        std::string to_buf(KVRequest reply);
        KVRequest from_buf(std::string buf);
        KVReply handle_request(KVRequest req);

    private:
        std::unordered_map<std::string, std::string> store_;
};

#endif
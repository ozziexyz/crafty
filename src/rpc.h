#ifndef RPC_H
#define RPC_H

#include <memory>
#include <asio.hpp>
#include "rpc.pb.h"
#include "framing.h"

using asio::ip::tcp;
using namespace crafty;

struct LogEntry {
    std::string data;
    int term;
};

enum RPCSessionType {
    READ,
    WRITE
};

struct AppendEntries {
    int leader_id;
    int current_term;        
    int commit_index;
    int prefix_length;
    int prefix_term;
    std::vector<LogEntry> suffix; 
};

struct AppendEntriesReply {
    int node_id;
    int current_term;        
    int ack;
    bool valid;
};

struct RequestVote {
    int node_id;
    int current_term;
    int last_index;
    int last_term;
};

struct RequestVoteReply {
    int node_id;
    int current_term;
    bool vote;
};

class RPCSession : public std::enable_shared_from_this<RPCSession> {
    public:
        RPCSession(tcp::socket socket, RPCSessionType type, std::function<std::string(std::string)> read_callback);
        RPCSession(
            tcp::socket socket, 
            RPCSessionType type, 
            std::function<void(std::string)> write_callback, 
            std::string data
        );
        void start();
    private:
        void do_read();
        void do_write();
        RPCSessionType type_;
        tcp::socket socket_;
        std::string data_in_;
        std::string data_out_;
        std::array<unsigned char, 4> header_;
        std::function<void(std::string)> write_callback_;
        std::function<std::string(std::string)> read_callback_;
};

class RPCTransport {
    public:
        RPCTransport(
            asio::io_context& io, 
            int port, 
            std::function<void(std::string)> write_callback, 
            std::function<std::string(std::string)> read_callback
        );
        void do_connect(std::string msg, int port);
    private:
        void do_accept();

        asio::io_context& io_;
        int port_;
        std::function<void(std::string)> write_callback_;
        std::function<std::string(std::string)> read_callback_;
        tcp::acceptor acceptor_;
};

class RPCService {
    public:
        RPCService(
            asio::io_context& io, 
            int port, 
            std::function<void(AppendEntriesReply)> ae_reply_callback, 
            std::function<AppendEntriesReply(AppendEntries)> ae_callback,
            std::function<void(RequestVoteReply)> rv_reply_callback, 
            std::function<RequestVoteReply(RequestVote)> rv_callback
        );
        void append_entries(AppendEntries msg, int peer);
        void request_vote(RequestVote msg, int peer);

    private:
        asio::io_context& io_;
        int port_;
        std::function<void(AppendEntriesReply)> ae_reply_callback_;
        std::function<AppendEntriesReply(AppendEntries)> ae_callback_;
        std::function<void(RequestVoteReply)> rv_reply_callback_;
        std::function<RequestVoteReply(RequestVote)> rv_callback_;
        RPCTransport transport_;
};

#endif
#include <iostream>
#include "rpc.h"

RPCSession::RPCSession(
    tcp::socket socket, 
    RPCSessionType type, 
    std::function<std::string(std::string)> read_callback
) : socket_(std::move(socket)), type_(type), read_callback_(read_callback) {}

RPCSession::RPCSession(
    tcp::socket socket, 
    RPCSessionType type, 
    std::function<void(std::string)> write_callback,
    std::string data
) : socket_(std::move(socket)), type_(type), data_out_(data), write_callback_(write_callback) {}

void RPCSession::start() {
    if(type_ == RPCSessionType::READ) {
        do_read();
    } else if(type_ == RPCSessionType::WRITE) {
        do_write();
    }
}

void RPCSession::do_read() {
    auto self(shared_from_this());

    data_in_.resize(1024);

    socket_.async_read_some(asio::buffer(data_in_),
        [this, self](std::error_code ec, std::size_t length) {
            if (ec) { std::cout << "Read error: " << ec.message() << std::endl; return;};
            data_in_.resize(length);
            if(type_ == RPCSessionType::READ) {
                do_write();
            } else if(type_ == RPCSessionType::WRITE) {
                write_callback_(data_in_);
            }
        });
}

void RPCSession::do_write() {
    auto self(shared_from_this());

    if(type_ == RPCSessionType::READ) {
        data_out_ = read_callback_(data_in_);
    }

    asio::async_write(socket_, asio::buffer(data_out_),
        [this, self](std::error_code ec, std::size_t length) {
            if (ec) { std::cout << "Write error: " << ec.message() << std::endl; return;};
            
            if(type_ == RPCSessionType::WRITE) {
                do_read();
            }
        });
}

RPCTransport::RPCTransport(
    asio::io_context& io, 
    int port, 
    std::function<void(std::string)> write_callback, 
    std::function<std::string(std::string)> read_callback
) : io_(io), port_(port), acceptor_(io, tcp::endpoint(tcp::v4(), port)), write_callback_(write_callback), read_callback_(read_callback) {
    do_accept();
}

void RPCTransport::do_accept() {
    acceptor_.async_accept(
        [this](std::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<RPCSession>(std::move(socket), RPCSessionType::READ, read_callback_)->start();
            } else {
                std::cout << "Accept error: " << ec.message() << std::endl;
            }
            do_accept();
        });
}

void RPCTransport::do_connect(std::string msg, int port) {
    auto resolver = std::make_shared<tcp::resolver>(io_);
    resolver->async_resolve("localhost", std::to_string(port), [this, msg, resolver](asio::error_code ec, tcp::resolver::results_type results){
        if (ec) { std::cout << "Resolve error: " << ec.message() << std::endl; return;};

        auto socket = std::make_shared<tcp::socket>(io_);
        asio::async_connect(*socket, results,
            [this, msg, socket](asio::error_code ec, const tcp::endpoint& endpoint) {
                if (ec) {
                    std::cout << "Connect error: " << ec.message() << std::endl;
                    return;
                }
                std::make_shared<RPCSession>(std::move(*socket), RPCSessionType::WRITE, write_callback_, msg)->start();
            });
    });
}

RPCService::RPCService(
    asio::io_context& io, 
    int port, 
    std::function<void(AppendEntriesReply)> ae_reply_callback, 
    std::function<AppendEntriesReply(AppendEntries)> ae_callback,
    std::function<void(RequestVoteReply)> rv_reply_callback, 
    std::function<RequestVoteReply(RequestVote)> rv_callback
) : io_(io), port_(port), ae_reply_callback_(ae_reply_callback), ae_callback_(ae_callback), rv_reply_callback_(rv_reply_callback), rv_callback_(rv_callback), transport_(io, port, 
    [this](std::string data){
        proto::rpc::Envelope envelope;
        if(!envelope.ParseFromString(data)){
            std::cout << "Error: malformed RPC envelope" << std::endl;
        } else {
            if(envelope.type() == proto::rpc::Envelope_MessageType_APPEND_ENTRIES_REPLY) {
                proto::rpc::AppendEntriesReply reply_buf;
                reply_buf.ParseFromString(envelope.data());
                AppendEntriesReply reply {
                    reply_buf.node_id(), 
                    reply_buf.current_term(), 
                    reply_buf.ack(), 
                    reply_buf.valid()
                };
                ae_reply_callback_(reply);
            } else if(envelope.type() == proto::rpc::Envelope_MessageType_REQUEST_VOTE_REPLY) {
                proto::rpc::RequestVoteReply reply_buf;
                reply_buf.ParseFromString(envelope.data());
                RequestVoteReply reply {
                    reply_buf.node_id(), 
                    reply_buf.current_term(), 
                    reply_buf.vote() 
                };
                rv_reply_callback_(reply);
            }
        };
    },
    [this](std::string data){
        proto::rpc::Envelope envelope_in;
        proto::rpc::Envelope envelope_out;
        std::string data_out;
        std::string envelope_string;
        
        if(!envelope_in.ParseFromString(data)) {
            std::cout << "Error: malformed RPC envelope";
        } else {
            if(envelope_in.type() == proto::rpc::Envelope_MessageType_APPEND_ENTRIES) {
                proto::rpc::AppendEntries msg_buf;
                msg_buf.ParseFromString(envelope_in.data());
                std::vector<proto::rpc::LogEntry> buf_suffix(msg_buf.suffix().begin(), msg_buf.suffix().end());
                std::vector<LogEntry> suffix;
                for(auto l : buf_suffix) {
                    suffix.push_back(LogEntry{l.data(), l.term()});
                }
                AppendEntries msg {
                    msg_buf.leader_id(),
                    msg_buf.current_term(),        
                    msg_buf.commit_index(),
                    msg_buf.prefix_length(),
                    msg_buf.prefix_term(),
                    suffix
                };
                
                AppendEntriesReply reply = ae_callback_(msg);
                proto::rpc::AppendEntriesReply reply_buf;
                reply_buf.set_node_id(reply.node_id);
                reply_buf.set_current_term(reply.current_term);
                reply_buf.set_ack(reply.ack);
                reply_buf.set_valid(reply.valid);
                data_out = reply_buf.SerializeAsString();

                envelope_out.set_type(proto::rpc::Envelope_MessageType_APPEND_ENTRIES_REPLY);
                envelope_out.set_data(data_out);
                return envelope_out.SerializeAsString();
            } 
            if(envelope_in.type() == proto::rpc::Envelope_MessageType_REQUEST_VOTE) {
                proto::rpc::RequestVote msg_buf;
                msg_buf.ParseFromString(envelope_in.data());
                RequestVote msg {
                    msg_buf.node_id(),
                    msg_buf.current_term(),        
                    msg_buf.last_index(),
                    msg_buf.last_term(),
                };
                
                RequestVoteReply reply = rv_callback_(msg);
                proto::rpc::RequestVoteReply reply_buf;
                reply_buf.set_node_id(reply.node_id);
                reply_buf.set_current_term(reply.current_term);
                reply_buf.set_vote(reply.vote);
                data_out = reply_buf.SerializeAsString();

                envelope_out.set_type(proto::rpc::Envelope_MessageType_REQUEST_VOTE_REPLY);
                envelope_out.set_data(data_out);
                return envelope_out.SerializeAsString();
            }
        }
        return std::string("invalid message type");
    })
{}

void RPCService::append_entries(AppendEntries msg, int peer) {
    proto::rpc::Envelope envelope;
    proto::rpc::AppendEntries msg_buf;
    std::string msg_string;
    std::string envelope_string;

    for(auto i : msg.suffix) {
        proto::rpc::LogEntry* entry = msg_buf.add_suffix();
        entry->set_term(i.term);
        entry->set_data(i.data);
    }
    
    msg_buf.set_leader_id(msg.leader_id);
    msg_buf.set_current_term(msg.current_term);
    msg_buf.set_commit_index(msg.commit_index);
    msg_buf.set_prefix_length(msg.prefix_length);
    msg_buf.set_prefix_term(msg.prefix_term);

    msg_string = msg_buf.SerializeAsString();
    envelope.set_data(msg_string);
    envelope.set_type(proto::rpc::Envelope_MessageType_APPEND_ENTRIES);
    envelope_string = envelope.SerializeAsString();

    transport_.do_connect(envelope_string, peer);
}

void RPCService::request_vote(RequestVote msg, int peer) {
    proto::rpc::Envelope envelope;
    proto::rpc::RequestVote msg_buf;
    std::string msg_string;
    std::string envelope_string;
    
    msg_buf.set_node_id(msg.node_id);
    msg_buf.set_current_term(msg.current_term);
    msg_buf.set_last_index(msg.last_index);
    msg_buf.set_last_term(msg.last_term);

    msg_string = msg_buf.SerializeAsString();
    envelope.set_data(msg_string);
    envelope.set_type(proto::rpc::Envelope_MessageType_REQUEST_VOTE);
    envelope_string = envelope.SerializeAsString();

    transport_.do_connect(envelope_string, peer);
}
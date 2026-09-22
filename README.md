# Crafty
A key-value store with Raft consensus written in C++17 using Asio, and Protobuf. Supports leader elections, log replication, persistence, and client redirection.

[![CI](https://github.com/ozziexyz/crafty/actions/workflows/ci.yml/badge.svg)](https://github.com/ozziexyz/crafty/actions/workflows/ci.yml)

## Build
Make sure you have [CMake](https://cmake.org/) and the [Protobuf](https://github.com/protocolbuffers/protobuf) compiler installed. This project also uses Asio, which gets pulled from [chriskohlhoff/asio](https://github.com/chriskohlhoff/asio.git) automatically during configuration.
```bash
git clone https://github.com/ozziexyz/crafty.git
cd crafty
mkdir build
cd build
cmake ..
make
cd ..
```

## Quickstart
### 1. Run the automated tests
```bash
cd build
ctest --output-on-failure
cd ..
```
This spins up a temporary 5-node cluster, kills the leader, and checks every acknowledged write survives.

### 2. Start a cluster

```bash
./scripts/run_cluster.sh
```

### 3. Send some requests

```bash
./build/crafty_client put name crafty
./build/crafty_client get name
# crafty
```

### 4. Kill the leader and watch it recover

```bash
grep -l "I'm the leader" logs/node*.log
# e.g. logs/node5552.log. Kill that one!
pkill -f "crafty_node 5552"
sleep 1
grep -l "I'm the leader" logs/node*.log   # a different node now
./build/crafty_client get name
# crafty
```

### 5. Stop the cluster
```bash
./scripts/stop_cluster.sh
```

## Architecture

### Raft

Crafty uses Raft, a consensus algorithm that is used to synchronize a log of changes in a state machine across a cluster of nodes. Raft is designed to strictly maintain log consistency, even if one or more nodes die or become seperated from the rest. In the case of Crafty, the state machine is a key-value store and the log records PUT and DELETE requests that modify it.

The nodes start by holding an election to select a leader among themselves. The leader of a cluster receives all write requests from clients, and distributes them to the follower nodes. When the leader receives a write request it appends the request to its log, but doesn't commit the changes to its state machine yet. Instead, it sends all the "new" log entries out to each follower node. The followers can either approve or deny a log entry the leader sends, and the leader will only commit a change to its state machine once a majority of nodes have approved it. 

Even when a client doesn't make a request, the leader periodically sends an empty list of new log entries to each follower as a "heartbeat" to tell them that it is still alive. If a follower doesn't receive anything from the leader after a set amount of time, it becomes a candidate and starts a new election. It first votes for itself, and then asks all the other nodes to vote for it. If a node recieves votes from a majority of the cluster, it becomes the leader and the log replication process resumes. 

Of course, there are a lot of fine details that make this algorithm work in practice. If you'd like to learn more about Raft, give the [original paper](https://web.stanford.edu/~ouster/cgi-bin/papers/raft-atc14) by Diego Ongaro and John Ousterhout a read.

### RPC
To communicate with eachother, Crafty nodes use a Protobuf-based RPC system. This system is broken up into 3 layers: transport, service, and node. The transport layer handles all the network IO, and is Raft-agnostic. It just sends and recieves raw data to and from the service layer. The service layer owns an `RPCTransport` object, and translates between raw Protobufs and message structs that the node layer can understand. The node owns an `RPCService` object and uses the messages to perform the election logic and log replication. 

There are four RPC message types that nodes can use to communicate with eachother:
- **`RequestVote`**

    Sent by candidate nodes during an election to request votes from other nodes

- **`RequestVoteReply`**

    Sent back to candidate nodes that requested a vote

- **`AppendEntries`**

    Sent by the leader node to follower nodes. Includes some log entries to append, or none if it's a heartbeat

- **`AppendEntriesReply`**

    Sent by follower nodes in response to an `AppendEntries` message

The node registers handler callbacks for each message type with the `RPCService`. When the service layer parses a message, it calls the handler that corresponds to the message's type. This way, the node can send requests asynchronously to other nodes. If the leader is broadcasting a heartbeat, or a candidate is broadcasting `RequestVote`s, the thread won't get blocked waiting for a reply. 

### Key-Value Store
Each node's has a key-value (KV) store server, and listens for requests on a different port than the RPC server. Similarly to the RPC server, the node registers a callback handler with the KV Server. However, leader nodes can't always immediately reply to the client and might need to broadcast AppendEntries to its followers before sending success/failure. Instead of expecting the callback to return a value, the service passes another callback as an argument. The node can either store this callback in a `pending_` map until it's sure that the entry has been committed, or respond immediately.  

Only clients can make KV requests to nodes, not other nodes. Since all requests have to go through the leader, the follower nodes need to redirect the client to the leader when they receive a request. Initially, the client doesn't know who is the leader is and has to guess. If a follower node receives a request, it will reply with the correct leader. If it doesn't know who the leader is or fails to respond to the client, the client will try a different node and the process repeats.

## Known Limitations
- No snapshots / log compaction. The log and state file grow without bound.
- Static cluster membership, fixed by `cluster.cfg`; no membership changes.
- Reads are served from the local node's state, so followers may return stale data
  (not linearizable!).
- No client request deduplication. A retried `PUT` after a timeout may apply twice.
- State file is written atomically (temp + rename) but not `fsync`ed, so it may not
  survive power loss.
- Client port is Raft port + 1110 (hard-coded); peers are resolved as `localhost`, so
  clusters can't span machines yet.
- No authentication or TLS on either port.
- Single-threaded by design (one `io_context`).

## Design Notes

**Single-threaded, no mutexes.** Crafty runs one `io_context` on one thread. Every RPC
and KV callback executes serially on it, so there are no locks anywhere. `log_`,
`store_`, `role_`, `next_index_`, `pending_`, and everything else are only ever touched
from that one thread. This was a deliberate choice made to avoid the added complexity of multi-thread synchronization.

**Async replies via stored callbacks.** The `pending_` pattern described above (under
Key-Value Store) is continuation-passing style: rather than block the single thread
waiting for a majority to replicate, the leader parks the client's callback and invokes
it later, from `commit()`

**Client-driven leader discovery.** Followers hint at the
leader instead of forwarding the request themselves (see Key-Value Store above). This creates simpler nodes at the cost of more retry logic living in the client. Many popular production-grade Raft databases use this approach, including [etcd](https://etcd.io/).
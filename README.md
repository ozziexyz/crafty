# Crafty | Key-Value Store with Raft Consensus


## Setup
Make sure you have [CMake](https://cmake.org/) and the [Protobuf](https://github.com/protocolbuffers/protobuf) compiler installed. This project also uses Asio, which gets pulled from [chriskohlhoff/asio](https://github.com/chriskohlhoff/asio.git) automatically during configuration.
```bash
git clone https://ozziexyz/crafty.git
cd crafty
mkdir build
cd build
cmake ..
make
```
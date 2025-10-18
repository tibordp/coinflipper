/*
    coinflipper - A program that flips coins

    Copyright (c) 2014 coinflipper contributors

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#include <iostream>
#include <string>
#include <thread>
#include <zmq.hpp>

#include <CLI/CLI.hpp>

#include "dummy_rng.h"

namespace coinflipper {

// A global ZMQ context
zmq::context_t context(1);

void enable_ipv6(zmq::socket_t& socket) {
#ifdef ZMQ_IPV6
    socket.set(zmq::sockopt::ipv6, 1);
#endif
}

int coin_flipper(const std::string&, unsigned);
int coin_server();
int coin_status(const std::string&, bool export_ = false);

} // namespace coinflipper

int main(int argc, char* argv[]) {
    using coinflipper::coin_flipper;
    using coinflipper::coin_server;
    using coinflipper::coin_status;

    CLI::App app{"Coinflipper - Distributed coin flipping for RNG benchmarking"};
    app.require_subcommand(1);
    app.set_version_flag("--version", "1.0.0");

    // Flipper subcommand
    std::string flipper_server;
    unsigned flipper_threads = 0; // 0 means use hardware_concurrency
    auto* flipper = app.add_subcommand("flipper", "Run coin flipping worker");
    flipper->add_option("server", flipper_server, "Server address (host:port or host)")->required();
    flipper->add_option("-j,--threads", flipper_threads, "Number of worker threads")
        ->default_val(0)
        ->check(CLI::PositiveNumber);
    flipper->callback([&]() {
        if (flipper_threads == 0) {
            flipper_threads = std::thread::hardware_concurrency();
        }
        std::cerr << "Starting " << flipper_threads << " worker threads" << std::endl;
        exit(coin_flipper(flipper_server, flipper_threads));
    });

    // Server subcommand
    auto* server = app.add_subcommand("server", "Run coin flipping server");
    server->callback([&]() { exit(coin_server()); });

    // Status subcommand
    std::string status_server;
    auto* status = app.add_subcommand("status", "Query server status");
    status->add_option("server", status_server, "Server address (host:port or host)")->required();
    status->callback([&]() { exit(coin_status(status_server, false)); });

    // Export subcommand
    std::string export_server;
    auto* export_cmd = app.add_subcommand("export", "Export server data");
    export_cmd->add_option("server", export_server, "Server address (host:port or host)")
        ->required();
    export_cmd->callback([&]() { exit(coin_status(export_server, true)); });

    CLI11_PARSE(app, argc, argv);

    return 0;
}

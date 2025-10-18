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

#include <algorithm>
#include <bitset>
#include <chrono>
#include <climits>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "coinflipper.h"
#include "coinflipper.pb.h"
#include "dummy_rng.h"

namespace coinflipper {

/*
    This class does the actual coin flipping. It seeds the RNG from
    a random device, such as /dev/random, then it simply counts streaks.
*/

template <typename Rng> class coin {
    async_results& results;

  public:
    coin(async_results& rslt_) : results(rslt_) {}

    void operator()() {
        /* We allocate the RNG inside the thread to get better performance */
        Rng rng;

        /* We can use random number generators with varying output
           integer size */

        const int bit_count = sizeof(decltype(rng())) * CHAR_BIT;
        const uint32_t iter_num = 0xffff;

        for (int i = 0; i < 64; ++i) {
            // Overkill, but minimizing the chances of a duplicate seed
            rng.seed(std::random_device()());
        }

        result_array current;
        current.fill(0);

        std::bitset<bit_count> cur_bits;
        unsigned count = 0;

        bool prev = true;

        for (uint32_t iteration = iter_num;; --iteration) {
            cur_bits = rng();

            for (int i = bit_count; i--;) {
                bool t = cur_bits[i];

                if (t == prev) {
                    ++count;
                } else {
                    prev = t;

                    /* It is unlikely that we'll ever encounter streaks longer than 127 */
                    if (count <= 127) {
                        ++current[count];
                    }

                    count = 0;
                }
            }

            /* Each 0xffff * 64 flips, we send an update */

            if (iteration == 0) {
                results.push(current, iter_num * bit_count);
                iteration = iter_num;
                current.fill(0);
            }
        }
    }
};

/*
    This class connect to the server and periodically sends the accumulated
    statistics from all threads.
*/

class coin_sender {
    async_results& results;
    std::string server_address;

  public:
    coin_sender(async_results& results_, const std::string& server_address_)
        : results(results_), server_address(server_address_) {};

    void operator()() {
        try {
            uint64_t hash = std::random_device()();

            std::cerr << "Started flipping the coins (my hash is: " << std::hex << hash << ") "
                      << std::endl;

            zmq::socket_t socket(context, ZMQ_PUSH);
            enable_ipv6(socket);

            /* We craft 0MQ URL from the server address */

            std::string url("tcp://");
            url += server_address;
            url += ":5555";

            socket.connect(url.c_str());

            for (;;) {
                auto rslt = results.get();

                coinflipper::coinbatch cf;

                cf.set_hash(hash);
                cf.set_total_flips(rslt.second);
                rslt.first.insert_to_pb(cf);

                size_t size = cf.ByteSizeLong();
                zmq::message_t request(size);
                cf.SerializeToArray(request.data(), size);
                socket.send(request, zmq::send_flags::none);

                results.pop();

                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } catch (...) {
            return;
        }
    }
};

int coin_flipper(const std::string& server_address, unsigned thread_count) {
    async_results results;
    std::vector<std::thread> threads;

    // Use specified thread count, or hardware concurrency if 0
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
    }

    // We create many workers.
    for (unsigned i = 0; i < thread_count; ++i) {
        threads.emplace_back(coin<std::mt19937_64>(results));
    }

    // We can have multiple senders but one suffices.
    threads.emplace_back(coin_sender(results, server_address));

    // Wait for workers to terminate.
    for (auto& i : threads) {
        i.join();
    }

    return 0;
}

} // namespace coinflipper

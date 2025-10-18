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

#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <utility>
#include <zmq.hpp>

namespace coinflipper {

// A global ZMQ context
extern zmq::context_t context;
void enable_ipv6(zmq::socket_t& socket);

/*
    result_array is a simple array container that holds the statistics
    It can be converted to any appropriate Protocol Buffers message that
    implements flips(), add_flips().
*/

class result_array : public std::array<uint64_t, 128> {

  public:
    template <typename T> void insert_to_pb(T& pb_message) const {
        for (int i = 0; i < 128; i++) {
            if ((*this)[i] != 0) {
                auto d = pb_message.add_flips();
                d->set_position(i);
                d->set_flips((*this)[i]);
            }
        }
    }

    template <typename T> static result_array create_from_pb(T& pb_message) {
        result_array result;
        result.fill(0);

        for (const auto& i : pb_message.flips()) {
            result[i.position()] = i.flips();
        }

        return result;
    }
};

/*
    async_results is a class that holds the current statistics about coinflips
    it is used by both the server and the worker nodes. It uses locking when
    it updates, so it can be updated by many threads at once.
*/

class async_results {
  private:
    result_array rslt;
    uint64_t total;
    mutable std::mutex mtx;

  public:
    async_results() {
        rslt.fill(0);
        total = 0;
    }

    void push(const result_array& val, uint64_t count) {
        std::lock_guard<std::mutex> lg(mtx);
        total += count;

        for (int i = 0; i < 128; ++i) {
            rslt[i] += val[i];
        }
    }

    std::pair<result_array, uint64_t> get() const {
        std::lock_guard<std::mutex> lg(mtx);
        return std::make_pair(rslt, total);
    }

    void pop() {
        std::lock_guard<std::mutex> lg(mtx);
        rslt.fill(0);
        total = 0;
    }
};

} // namespace coinflipper

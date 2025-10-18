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

#include <bitset>
#include <cstdint>

/* Can be replaced by __builtin_ctzll() or another suitable intrinsic on
   hardware/compilers that support it */

inline int ctzll(uint64_t state) {
    for (int i = 0; i < 64; ++i) {
        if (state & ((uint64_t) 1 << i)) {
            return i;
        }
    }
    return 64;
}

/*
    This class is a dummy RNG that produces a sequence where streaks are perfectly
    exponentially distributed. Useful for debugging.

    Principle of operation:

    000000011111111
    000111100001111     We take the binary representation of consecutive numbers
    011001100110011
    101010101010101

           4
      3       3        We find the position of the leftmost 1 bit (=n).
     2   2   2   2
    1 1 1 1 1 1 1 1

    121312141213121

    By repeating alternating bits n times, we produce a stream of bits:
    0 1 1 0 1 1 1 0 1 1 0 1 1 1 1 0 1 1 0 1 1 1 0 1 1 0

    Mind that while this will give a perfect score with coinflipper, it is clearly
    a very poor RNG, since it has a huge bias towards 1.
*/

class dummy_rng_distributed {
    bool cur_bit;
    uint64_t state;
    unsigned remaining;

  public:
    void seed(uint64_t state_) {
        state = state_;
    }

    dummy_rng_distributed() : cur_bit(false), state(0), remaining(0) {}

    inline uint64_t operator()() {
        std::bitset<64> result(0);

        for (int i = 64; i--;) {
            if (!remaining) {
                ++state;
                cur_bit = !cur_bit;
                remaining = ctzll(state) + 1;
            }

            result.set(i, cur_bit);
            --remaining;
        }

        return result.to_ullong();
    }
};

/*
    This class is a dummy RNG that produces a sequence of streaks with length
    streak_length
*/

template <unsigned streak_length> class dummy_rng_simple {
    bool cur_bit;
    unsigned remaining;

  public:
    void seed(uint64_t state_) {
        remaining = state_ % streak_length;
    }

    dummy_rng_simple() : cur_bit(false), remaining(0) {}

    inline uint64_t operator()() {
        std::bitset<64> result(0);

        for (int i = 64; i--;) {
            if (!remaining) {
                cur_bit = !cur_bit;
                remaining = streak_length;
            }

            result.set(i, cur_bit);
            --remaining;
        }

        return result.to_ullong();
    }
};

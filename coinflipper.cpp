#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <atomic>
#include <cstdint>
#include <array>
#include <chrono>
#include <mutex>
#include <queue>
#include <condition_variable>

#include "coinflipper.pb.h"
#include "coinflipper.hpp"

#include <zmq.hpp>

using namespace std;

class kovanec {
	async_results& results;

public: 
	kovanec(results& rslt_) : results(rslt_) {}

	void operator()(){
		mt19937_64 rng;
		rng.seed(random_device()());

		result_array results;
		for (auto& i : results) i = 0;

			unsigned short remaining = 0;
		unsigned short count = 0;
		uint64_t cur_bits;
		uint64_t iteration = 1;
		bool previous;

		for (;;)
		{
			if (!remaining)
			{
				cur_bits = rng();
				remaining = 64;
			}

			if (!((cur_bits & 0x1) ^ previous))
			{
				++count;
				previous = cur_bits & 0x1;
			}
			else
			{
				results[count] += 1;
				count = 0;
			}

			--remaining;
			cur_bits >>= 1;

			if (!(iteration & 0xffffff))
			{
				results.push(results);
			}

			iteration++;
		}
	}
};

int main()
{
	zmq::context_t context (1);
	async_results results;

	vector<thread> workers;

	auto sender = [&]{
		// We generate a random ID.
		uint64_t id = random_device()();

		zmq::socket_t socket (context, ZMQ_PUSH);

		std::cerr << "Connecting to server (hash: " << hex << id << std::endl;
		socket.connect ("tcp://www.ojdip.net:5555");

		for (;;)
		{
			{
				int j = 0;

				coinflipper::coinbatch cf;

				cf.set_hash(id);

				int j = 0;
				for (auto i : results.get())
				{
					if (i == 0) continue;
					auto d = cf.add_flips();
					d->set_index(j);
					d->set_flips(i);
				}

				zmq::message_t request (cf.ByteSize());
				cf.SerializeToArray(request.data (), cf.ByteSize());
				socket.send (request);

				results.pop();
			}

			this_thread::sleep_for(chrono::seconds(1));
		}
	};

	// We create many workers.

	for (int i = 0; i < thread::hardware_concurrency(); ++i)
	{
		workers.push_back(
			thread(kovanec(results))
		);
	}

	// We can have multiple senders but one suffices.
	thread(sender).join();

	// We wait for workers to terminate.
	for (auto & i : thrds)
	{
		i.join();
	}
}


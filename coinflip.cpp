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

#include "pb.pb.h"
#include "coinflip.hpp"

#include <zmq.hpp>

using namespace std;

class kovanec {
	results& rslt;

public: 
	kovanec(results& rslt_) : rslt(rslt_) {}

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
				rslt.push(results);
			}

			iteration++;
		}
	}
};

int main()
{
	zmq::context_t context (1);

	bool updating = false;
	results rslt;

	vector<thread> thrds;

	for (int i = 0; i < thread::hardware_concurrency(); ++i)
	{
		thrds.push_back(thread(kovanec(rslt)));
	}

	auto sender = [&]{
		uint64_t id = random_device()();

		zmq::socket_t socket (context, ZMQ_PUSH);

		std::cerr << "Connecting to server" << std::endl;
		socket.connect ("tcp://celovs01.ojdip.net:5555");

		for (;;)
		{
			int j = 0;

			auto results = rslt.get();

			coinflipper::coinbatch cf;

			cf.set_hash(id);

			for (auto &i : results)
			{
				if (i == 0) continue;
				auto d = cf.add_flips();
				d->set_index(j);
				d->set_flips(i);
			}

			zmq::message_t request (cf.ByteSize());
			cf.SerializeToArray(request.data (), cf.ByteSize());

			socket.send (request);

			this_thread::sleep_for(chrono::seconds(1));
		}
	};

	thrds.push_back(thread(sender));

	for (auto & i : thrds)
	{
		i.join();
	}
}


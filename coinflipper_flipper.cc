#include <algorithm>
#include <iostream>
#include <bitset>
#include <chrono>
#include <iomanip>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <climits>
#include <cstdint>

#include "coinflipper.pb.h"
#include "coinflipper.h"

#include "dummy_rng.h"

using namespace std;

/*
	This class does the actual coin flipping. It seeds the RNG from 
	a random device, such as /dev/random, then it simply counts streaks.
*/	

template<typename Rng> class coin {
	async_results& results;

public:
	coin(async_results& rslt_) : results(rslt_) {}

	void operator()(){
		Rng rng;

		/* We can use random number generators with varying output
		   integer size */

		const int      bit_count = sizeof(decltype(rng())) * CHAR_BIT;
		const uint32_t iter_num = 0xffff;

		rng.seed(random_device()());

		result_array current;
		current.fill(0);

		bitset<bit_count> cur_bits;
		unsigned count = 0;

		bool prev = true;

		for (uint32_t iteration = iter_num;; --iteration)
		{
			cur_bits = rng();

			for (int i = bit_count; i--;)
			{
				bool t = cur_bits[i];

				if (t == prev)
					++count;
				else
				{
					prev = t;
					
					/* It is unlikely that we'll ever encounter streaks longer than 127 */
					if (count <= 127)
						++current[count];
		
					count = 0;
				}
			}

			/* Each 0xffff * 64 flips, we send an update */

			if (iteration == 0)
			{
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

class coin_sender
{
	async_results& results;
	string server_address;

public:
	coin_sender(async_results& results_,
		const string& server_address_) :
	results(results_),
	server_address(server_address_) {};

	void operator()() {
		try
		{
			uint64_t hash = random_device()();

			cerr << "Started flipping the coins (my hash is: " << hex << hash << ") " << endl;

			zmq::socket_t socket(context, ZMQ_PUSH);
			enable_ipv6(socket);

			/* We craft 0MQ URL from the server address */

			string url("tcp://");
			url += server_address;
			url += ":5555";

			socket.connect(url.c_str());

			for (;;)
			{
				auto rslt = results.get();

				coinflipper::coinbatch cf;

				cf.set_hash(hash);
				cf.set_total_flips(rslt.second);
				rslt.first.insert_to_pb(cf);

				zmq::message_t request(cf.ByteSize());
				cf.SerializeToArray(request.data(), cf.ByteSize());
				socket.send(request);

				results.pop();

				this_thread::sleep_for(chrono::seconds(1));
			}
		}
		catch (...)
		{
			return;
		}
	}
};

int coin_flipper(const string& server_address)
{
	async_results results;
	vector<thread> threads;

	// We create many workers.
	for (unsigned i = 0; i < thread::hardware_concurrency(); ++i)
		threads.push_back(thread(coin<mt19937_64>(results)));

	// We can have multiple senders but one suffices.
	threads.push_back(thread(coin_sender(results, server_address)));

	// Wait for workers to terminate.
	for (auto & i : threads)
		i.join();

	return 0;
}

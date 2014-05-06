#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <atomic>
#include <cstdint>
#include <string>
#include <array>
#include <chrono>
#include <mutex>
#include <queue>
#include <condition_variable>

#include "coinflipper.h"
#include "coinflipper.pb.h"

#include <zmq.hpp>

using namespace std;

template<typename Rng>
class coin {
	async_results& results;

public: 
	coin(async_results& rslt_) : results(rslt_) {}

	void operator()(){
		Rng rng;

		/* We can use random number generators with varying output 
		   integer size */

		using RngInt = decltype(rng());
		rng.seed(random_device()());

		result_array current;
		current.fill(0);

		unsigned remaining = 0;
		unsigned count = 0;

		RngInt cur_bits;

		bool previous;

		for (uint64_t iteration = 1 ;; ++iteration)
		{
			if (!remaining)
			{
				cur_bits = rng();
				remaining = sizeof(RngInt);
			}

			if (!((cur_bits & 0x1) ^ previous))
				++count;
			else
			{
				++current[count];
				count = 0;
			}

			previous = cur_bits & 0x1;

			--remaining;
			cur_bits >>= 1;

			/* Each 0xffffff flips, we send an update */

			if (!(iteration & 0xffffff))
			{
				results.push(current);
				current.fill(0);
			}
		}
	}
};

class coin_sender
{
	zmq::context_t& context;
	async_results& results;

public:
	coin_sender(zmq::context_t& context_, 
		async_results& results_) : 
	context(context_),
	results(results_) {};

	void operator()() {
		try 
		{
			uint64_t id = random_device()();

			zmq::socket_t socket (context, ZMQ_PUSH);
			cerr << "Connecting to server (hash: " << hex << id << ") " << endl;
			
			int ipv6 = 1;
			socket.setsockopt(ZMQ_IPV6, &ipv6, sizeof(int));

			socket.connect("tcp://[::1]:5555");

			for (;;)
			{
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

				this_thread::sleep_for(chrono::seconds(1));
			}
		} 
		catch (...)
		{	
			return;		
		}
	}
};

int coin_flipper()
{
	zmq::context_t context (1);
	async_results results;

	vector<thread> workers;

	// We create many workers.
	for (unsigned i = 0; i < thread::hardware_concurrency(); ++i)
		workers.push_back(thread(coin<mt19937_64>(results)));

	// We can have multiple senders but one suffices.
	thread(coin_sender(context, results)).join();

	// We wait for workers to terminate.
	for (auto & i : workers)
	{
		i.join();
	}

	return 0;
}

/* ------------------------------------------------------------------------- */

class coin_listener 
{
	zmq::context_t& context;
	async_results& results;

public:

	coin_listener(zmq::context_t& context_, 
		async_results& results_) : 
	context(context_),
	results(results_) {};

	void insert_work(const coinflipper::coinbatch& cf) {
		result_array work;

		for (auto& i : work) 
			i = 0;

		for (const auto& i : cf.flips())
		{
			work[i.index()] = i.flips();
		}

		results.push(work);
	}

	void operator()() {
		try 
		{
			zmq::socket_t socket (context, ZMQ_PULL);
			
			int ipv6 = 1;
			socket.setsockopt(ZMQ_IPV6, &ipv6, sizeof(int));

			socket.bind ("tcp://*:5555");

			while (true) {
				zmq::message_t update;
				socket.recv (&update);

				coinflipper::coinbatch cf;
				cf.ParseFromArray(update.data(), update.size());

				insert_work(cf);
				cout << "Received work (id: " << hex << cf.hash() << ")" << endl;
			}
		} 
		catch (...)
		{	
			return;		
		}
	}
};

int coin_server() {
	zmq::context_t context (1);
	async_results results;

	vector<thread> threads;

	threads.push_back(thread(coin_listener(
		context, results)));

	for (auto &i : threads) 
		i.join();

	return 0;
}

/* ------------------------------------------------------------------------- */

int main(int argc, char* argv[])
{
	switch (argc) {
		case 0:
		case 1:
		return coin_flipper();
		case 2: 
		if (string(argv[1]) == "server")
			return coin_server();
		default:
		cerr << "Usage: coinflipper [server]" << endl;
		return 1;
	}
}
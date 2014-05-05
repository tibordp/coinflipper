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

class kovanec {
	async_results& results;

public: 
	kovanec(async_results& rslt_) : results(rslt_) {}

	void operator()(){
		mt19937_64 rng;
		rng.seed(random_device()());

		result_array current;
		for (auto& i : current) i = 0;

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
				current[count] += 1;
				count = 0;
			}

			--remaining;
			cur_bits >>= 1;

			if (!(iteration & 0xffffff))
			{
				results.push(current);
			}

			iteration++;
		}
	}
};

class socket_man 
{
	zmq::context_t& context;
	async_results& results;

public:

	socket_man(zmq::context_t& context_, 
		async_results& results_) : 
	context(context_),
	results(results_) {};

	void operator()() {
		try 
		{
			zmq::socket_t socket (context, ZMQ_PULL);
			socket.bind ("tcp://*:5555");

			while (true) {
				zmq::message_t update;

        		//  Wait for next request from client
				socket.recv (&update);

				coinflipper::coinbatch cf;
				cf.ParseFromArray(update.data(), update.size());

				result_array work;

				for (auto& i : work) 
					i = 0;

				for (const auto& i : cf.flips())
				{
					work[i.index()] = i.flips();
				}

				results.push(work);

				cout << "Received work (id: " << hex << cf.hash() << ")" << endl;
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

	auto sender = [&]{
		// We generate a random ID.
		uint64_t id = random_device()();

		zmq::socket_t socket (context, ZMQ_PUSH);

		cerr << "Connecting to server (hash: " << hex << id << ") " << endl;

		socket.connect("tcp://www.ojdip.net:5555");

		for (;;)
		{
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
			}

			this_thread::sleep_for(chrono::seconds(1));
		}
	};

	// We create many workers.

	for (unsigned i = 0; i < thread::hardware_concurrency(); ++i)
	{
		workers.push_back(
			thread(kovanec(results))
			);
	}

	// We can have multiple senders but one suffices.
	thread(sender).join();

	// We wait for workers to terminate.
	for (auto & i : workers)
	{
		i.join();
	}

	return 0;
}

int coin_server() {
	zmq::context_t context (1);
	async_results results;

	vector<thread> threads;

	threads.push_back(thread(socket_man(
		context, results)));

	for (auto &i : threads) 
		i.join();

	return 0;
}

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
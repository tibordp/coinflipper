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

// Because ZMQ bindings are daft and require pointers
int P0 = 0;
int P1 = 1;

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

		bool previous = true;

		for (uint64_t iteration = 0 ;; ++iteration)
		{
			if (!remaining)
			{
				cur_bits = rng();
				remaining = sizeof(RngInt);
			}

			if ((cur_bits & 0x1) ^ previous)
			{
				++current[count];
				count = 0;
			}
			else 
				++count;

			previous = cur_bits & 0x1;

			--remaining;
			cur_bits >>= 1;

			/* Each 0xffffff flips, we send an update */

			if (iteration == 0xffffff)
			{
				results.push(current, iteration);
				current.fill(0);

				iteration = 0;
			}
		}
	}
};

class coin_sender
{
	zmq::context_t& context;
	async_results& results;

	string server_address;
public:
	coin_sender(zmq::context_t& context_, 
		async_results& results_, 
		const string& server_address_) : 
	context(context_),
	results(results_),
	server_address(server_address_) {};

	void operator()() {
		try 
		{
			uint64_t id = random_device()();
			
			cerr << "Connecting to server (my hash is: " << hex << id << ") " << endl;

			zmq::socket_t socket (context, ZMQ_PUSH);
			socket.setsockopt(ZMQ_IPV4ONLY, &P0, sizeof(int));

			string url("tcp://");
			url += server_address;
			url += ":5555";
			
			cout << "\"" << url << "\"";

			socket.connect(url.c_str());

			for (;;)
			{
				coinflipper::coinbatch cf;

				auto rslt = results.get();

				cf.set_hash(id);
				cf.set_total_flips(rslt.second);
				rslt.first.insert_to_pb(cf);

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

int coin_flipper(char* server_address)
{
	zmq::context_t context (1);
	async_results results;

	vector<thread> workers;

	// We create many workers.
	for (unsigned i = 0; i < thread::hardware_concurrency(); ++i)
		workers.push_back(thread(coin<mt19937_64>(results)));

	// We can have multiple senders but one suffices.
	thread(coin_sender(context, results, server_address)).join();

	// We wait for workers to terminate.
	for (auto & i : workers)
	{
		i.join();
	}

	return 0;
}

/* ------------------------------------------------------------------------- */

class coin_listener_workers 
{
	zmq::context_t& context;
	async_results& results;

public:

	coin_listener_workers(zmq::context_t& context_, 
		async_results& results_) : 
	context(context_),
	results(results_) {};

	void insert_work(const coinflipper::coinbatch& cf) {
		result_array work;
		work.fill(0);

		for (const auto& i : cf.flips())
		{
			work[i.index()] = i.flips();
		}

		results.push(work, cf.total_flips());
	}

	void operator()() {
		try 
		{
			zmq::socket_t socket (context, ZMQ_PULL);
			socket.setsockopt(ZMQ_IPV4ONLY, &P0, sizeof(int));
			socket.bind ("tcp://*:5555");

			while (true) {
				zmq::message_t update;
				socket.recv (&update);

				coinflipper::coinbatch cf;
				cf.ParseFromArray(update.data(), update.size());

				insert_work(cf);
				// cout << "Received work (id: " << hex << cf.hash() << ")" << endl;
			}
		} 
		catch (...)
		{	
			return;		
		}
	}
};


class coin_listener_timer
{
	async_results& results;
	double& coins_per_second;
	
	decltype(chrono::high_resolution_clock::now()) previous_time;
	uint64_t previous_count;
public:
	coin_listener_timer(async_results& results_, double& coins_per_second_) : 
	results(results_), coins_per_second(coins_per_second_) {};

	void operator()() {
		for (;;)
		{
			using namespace chrono;

			auto rslt = results.get();

			auto flip_delta = rslt.second - previous_count;
			auto time_delta = high_resolution_clock::now() - previous_time;

			auto tt = duration_cast<nanoseconds>(time_delta);
			
			coins_per_second = flip_delta / ((double)tt.count() / 1000000000);

			previous_count += flip_delta;
			previous_time += time_delta;

			this_thread::sleep_for(seconds(5));
		}
	}

	double get_fps()
	{
		return coins_per_second;
	}
};

class coin_listener_clients 
{
	zmq::context_t& context;
	async_results& results;
	double& coins_per_second;

public:

	coin_listener_clients(zmq::context_t& context_, 
		async_results& results_,
		double& coins_per_second_) : 
	context(context_),
	results(results_),
	coins_per_second(coins_per_second_) {};

	void operator()() {
		try 
		{
			zmq::socket_t socket (context, ZMQ_REP);
		
			socket.setsockopt(ZMQ_IPV4ONLY, &P0, sizeof(int));

			socket.bind ("tcp://*:5556");

			while (true) {
				zmq::message_t update;
				socket.recv (&update);

				coinflipper::coinstatus cf;
				auto rslt = results.get();
				
				cf.set_total_flips(rslt.second);
				cf.set_flips_per_second(coins_per_second);

				rslt.first.insert_to_pb(cf);

				zmq::message_t request (cf.ByteSize());
				cf.SerializeToArray(request.data (), cf.ByteSize());
				
				socket.send (request);
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

	double cps = 0;

	threads.push_back(thread(coin_listener_timer(results, cps)));
	threads.push_back(thread(coin_listener_workers(context, results)));
	threads.push_back(thread(coin_listener_clients(context, results, cps)));

	for (auto &i : threads) 
		i.join();

	return 0;
}

/* ------------------------------------------------------------------------- */

#include <sstream>
#include <iomanip>

template<class T>
string commify(T value){
	struct punct: public std::numpunct<char>{
	protected:
		virtual char do_thousands_sep() const{return ',';}
		virtual std::string do_grouping() const{return "\03";}
	};
	std::stringstream ss;
	ss.imbue({std::locale(), new punct});
	ss << std::setprecision(0) << std::fixed << value;
	return ss.str();
}

int coin_status(char* server_address) {
	zmq::context_t context (1);

	zmq::socket_t socket (context, ZMQ_REQ);
	socket.setsockopt(ZMQ_IPV4ONLY, &P0, sizeof(int));

	string url("tcp://");
		url += server_address;
		url += ":5556";

	socket.connect(url.c_str());
	
	// We send an "empty" requests - just ping.
	zmq::message_t request (0);
	socket.send (request);
	
	zmq::message_t update;
	socket.recv (&update);

	coinflipper::coinstatus cf;
	cf.ParseFromArray(update.data(), update.size());
	
	auto results = result_array::create_from_pb(cf);

	// ... and we print it out.

	cout << "Total coins flipped:\t" << dec << commify(cf.total_flips()) << endl;
	cout << "Coins per second:\t" << dec << commify(cf.flips_per_second()) << endl << endl;

	for (int i = 0; i < 32; ++i)
	{
		cout << dec << i << ": \t" << commify(results[i]) << "\t\t"
		<< (i + 32) << ": \t" << commify(results[i + 32]) << endl;
	}

	return 0;
}

/* ------------------------------------------------------------------------- */

int main(int argc, char* argv[])
{
	switch (argc) 
	{
		case 3:
		if (string(argv[1]) == "flipper")
			return coin_flipper(argv[2]);
		if (string(argv[1]) == "status")
			return coin_status(argv[2]);
		case 2: 
		if (string(argv[1]) == "server")
			return coin_server();
	
		default:
		cerr << "Usage: coinflipper [flipper|server|status] <server>" << endl;
		return 1;
	}
}
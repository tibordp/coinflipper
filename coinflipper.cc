#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <cstdint>
#include <ctime>

#include "coinflipper.pb.h"
#include <zmq.hpp>

#include "coinflipper.h"

using namespace std;

// Because ZMQ bindings are daft and require pointers
int P0 = 0;
int P1 = 1;

// A global ZMQ context
zmq::context_t context(1);

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

		using RngInt = decltype(rng());
		rng.seed(random_device()());

		result_array current;
		current.fill(0);

		RngInt cur_bits;
		unsigned count = 0;

		bool prev = true;
		static const uint32_t iter_num = 0xffffff;

		for (uint32_t iteration = iter_num;; --iteration)
		{
			cur_bits = rng();

			for (int i = sizeof (RngInt); i--;)
			{
				bool t = (cur_bits & ((RngInt)1 << i)) == 0;
				if (t == prev)
					++count;
				else
				{
					prev = t;
					++current[count];
					count = 0;
				}
			}

			/* Each 0xffffff * 64 flips, we send an update */

			if (iteration == 0)
			{
				results.push(current, iter_num * sizeof (RngInt));
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

			cerr << "Connecting to server (my hash is: " << hex << hash << ") " << endl;

			zmq::socket_t socket(context, ZMQ_PUSH);
#ifdef ZMQ_IPV6
			socket.setsockopt(ZMQ_IPV6, &P1, sizeof(P1));
#else
			socket.setsockopt(ZMQ_IPV4ONLY, &P0, sizeof(P0));
#endif

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
	vector<thread> workers;

// We create many workers.
	for (unsigned i = 0; i < thread::hardware_concurrency(); ++i)
		workers.push_back(thread(coin<mt19937_64>(results)));

// We can have multiple senders but one suffices.
	thread(coin_sender(results, server_address)).join();

// We wait for workers to terminate.
	for (auto & i : workers)
	{
		i.join();
	}

	return 0;
}

/* ------------------------------------------------------------------------- */

/*
This class listens for client connections that send coinflip updates.
*/

class coin_listener_workers
{
	async_results& results;

public:
	coin_listener_workers(async_results& results_) : results(results_) {};

	void operator()() {
		try
		{
			zmq::socket_t socket(context, ZMQ_PULL);
#ifdef ZMQ_IPV6
			socket.setsockopt(ZMQ_IPV6, &P1, sizeof(P1));
#else
			socket.setsockopt(ZMQ_IPV4ONLY, &P0, sizeof(P0));
#endif
			socket.bind("tcp://*:5555");

			while (true) {
				zmq::message_t update;
				socket.recv(&update);

				coinflipper::coinbatch cf;
				cf.ParseFromArray(update.data(), update.size());

				results.push(result_array::create_from_pb(cf), cf.total_flips());
			}
		}
		catch (...)
		{
			return;
		}
	}
};

/*
This class implements a thread that periodically computes the current
coinflip rate.
*/

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

			auto tt = duration_cast<microseconds>(time_delta);
			coins_per_second = flip_delta / ((double)tt.count() / 1000000);

			previous_count += flip_delta;
			previous_time += time_delta;

			/* We choose a larger default time interval so that we don't sample
		   	   in a single update interval. */

			this_thread::sleep_for(seconds(5));
		}
	}
};

/*
This class listens for client connections that request status updates.
*/

class coin_listener_clients
{
	async_results& results;
	double& coins_per_second;

public:
	coin_listener_clients(async_results& results_,
		double& coins_per_second_) :
	results(results_),
	coins_per_second(coins_per_second_) {};

	void operator()() {
		try
		{
			zmq::socket_t socket(context, ZMQ_REP);

		#ifdef ZMQ_IPV6
			socket.setsockopt(ZMQ_IPV6, &P1, sizeof(P1));
		#else
			socket.setsockopt(ZMQ_IPV4ONLY, &P0, sizeof(P0));
		#endif

			socket.bind("tcp://*:5556");

			while (true) {
				zmq::message_t update;
				socket.recv(&update);

				coinflipper::coinstatus cf;
				auto rslt = results.get();

				cf.set_total_flips(rslt.second);
				cf.set_flips_per_second(coins_per_second);

				rslt.first.insert_to_pb(cf);

				zmq::message_t request(cf.ByteSize());
				cf.SerializeToArray(request.data(), cf.ByteSize());

				socket.send(request);
			}
		}
		catch (...)
		{
			return;
		}
	}
};


int coin_server() {
	async_results results;

	{
		ifstream current_status("status.cf", ios::binary);
		if (current_status.good())
		{
			coinflipper::coinstatus cf;
			cf.ParseFromIstream(&current_status);
			auto rslt = result_array::create_from_pb(cf);
			results.push(rslt, cf.total_flips());
		}
	}

	vector<thread> threads;

	double cps = 0;

	threads.push_back(thread(coin_listener_timer(results, cps)));
	threads.push_back(thread(coin_listener_workers(results)));
	threads.push_back(thread(coin_listener_clients(results, cps)));

	// We have a thread that exports and saves status every n minutes
	threads.push_back(thread([&]
	{
		// We fetch the current status
		for (;;)
		{
			auto rslt = results.get();
			coinflipper::coinstatus cf;

			cf.set_total_flips(rslt.second);
			cf.set_flips_per_second(cps);
			rslt.first.insert_to_pb(cf);

			// We generate the appropriate filename

			auto cur_time = time(nullptr);

			/* TODO: Get rid of this ugly C-style datetime handling. */
	    	char formatted_date[128];
	    	strftime(formatted_date, sizeof(formatted_date), "%Y_%m_%d_%H_%M_%S", gmtime(&cur_time));

			stringstream ss;
			ss << "history/status_" << formatted_date << ".cf";
			
			{
				ofstream status(ss.str(), ios::binary);
				cf.SerializeToOstream(&status);
			}
			{
				ofstream status("status.cf", ios::binary);
				cf.SerializeToOstream(&status);
			}

			this_thread::sleep_for(chrono::minutes(5));
		}
	}));

	for (auto &i : threads)
		i.join();

	return 0;
}

/* ------------------------------------------------------------------------- */

/*
This function synchronously requests a status update from the server.
*/

string timeify(uint64_t seconds) 
{
	stringstream ss;
	auto days =  seconds / (3600 * 24); seconds %= (3600 * 24);
	auto hours = seconds / (3600); seconds %= (3600);
	auto minutes = seconds / (60); seconds %= (60);

	if (days != 0) ss << days << " days ";
	if (hours != 0) ss << hours << " hours ";
	if (minutes != 0) ss << minutes << " minutes ";
	if (seconds != 0) ss << seconds<< " seconds ";

	return ss.str();
}

/* A human-readable textual output of the current status */

void coin_print_status(const coinflipper::coinstatus& cf)
{
	/* Some general statistics */

	auto results = result_array::create_from_pb(cf);
	cout << dec;
	
	auto total_flips = commify(cf.total_flips());
	auto fps = commify(cf.flips_per_second());

	cout << "Total coins flipped: " << 
		setw(max(total_flips.size(), fps.size())) << commify(cf.total_flips()) << endl;
	cout << "Coins per second:    " << 
		setw(max(total_flips.size(), fps.size())) << commify(cf.flips_per_second()) << endl << endl;

	/* We calculate the time remaining to next "decimal milestone" - that is 10^n total coinflips
	   Milestones obviously get exponentially harderto reach 
	 */

	double milestone = log(cf.total_flips()) / log(10);
	double rest = pow(10, ceil(milestone)) - cf.total_flips();
	double remaining = rest / cf.flips_per_second();

	if  (total_flips.size() != 0 && cf.flips_per_second() != 0)
		cout << "Time remaining to next milestone: " << timeify(remaining)  << endl << endl;

	/* We print the table in four columns, each with numbers aligned to the right */

	array<size_t, 4> maximal{ 0, 0, 0, 0 };
	array<string, 128> values;

	for (int i = 0; i < 128; ++i)
	{
		values[i] = commify(results[i]);
		maximal[i / 32] = max(maximal[i / 32], values[i].size());
	}

	for (int i = 0; i < 32; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			cout << setw(3) << i + (32 * j) << ": " << setw(maximal[j]) << values[i + (32 * j)];
			if (j < 3) cout <<  "        ";
		}
		cout << endl;
	}
}

int coin_status(const string& server_address, bool export_data = false) 
{
	zmq::socket_t socket(context, ZMQ_REQ);

#ifdef ZMQ_IPV6
	socket.setsockopt(ZMQ_IPV6, &P1, sizeof(P1));
#else
	socket.setsockopt(ZMQ_IPV4ONLY, &P0, sizeof(P0));
#endif

	string url("tcp://");
	url += server_address;
	url += ":5556";

	socket.connect(url.c_str());

	// We send an "empty" request - just a ping.

	zmq::message_t request(0);
	socket.send(request);

	zmq::message_t update;
	socket.recv(&update);

	coinflipper::coinstatus cf;
	cf.ParseFromArray(update.data(), update.size());

	if (export_data)
		cf.SerializeToOstream(&cout);
	else
		coin_print_status(cf);

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
		if (string(argv[1]) == "export")
			return coin_status(argv[2], true);

		case 2:
		if (string(argv[1]) == "server")
			return coin_server();

		default:
		cerr << "Usage: coinflipper [flipper|server|status|export] <server>" << endl;
		return 1;
	}
}

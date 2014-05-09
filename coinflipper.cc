#include <algorithm>
#include <array>
#include <bitset>
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

inline int ctzll(uint64_t state)	{
	for (int i = 0; i < 64; ++i)
	{
		if (state & ((uint64_t)1 << i))
			return i;
	}
	return 64;
}

class dummy_rng
{
	bool cur_bit;
	uint64_t state;
	unsigned remaining;

public:
	void seed(uint64_t state_) {
		state = state_;
	}

	dummy_rng() : cur_bit(false), state(0), remaining(0) {}

	inline uint64_t operator()() {
		bitset<64> result(0);

		for (int i = 64; i--; )
		{
			if (!remaining)
			{
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

		const int      bit_count = sizeof(decltype(rng())) * 8;
		const uint32_t iter_num = 0xffffff;

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
					++current[count];
					count = 0;
				}
			}

			/* Each 0xffffff * 64 flips, we send an update */

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
	//for (unsigned i = 0; i < thread::hardware_concurrency(); ++i)
	for (unsigned i = 0; i < 1; ++i)
		//workers.push_back(thread(coin<mt19937_64>(results)));
		workers.push_back(thread(coin<dummy_rng>(results)));

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

	array<size_t, 4> maximal{{ 0, 0, 0, 0 }};
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
			cout << setw(3) << i + (32 * j) + 1 << ": " << setw(maximal[j]) << values[i + (32 * j)];
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

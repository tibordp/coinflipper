#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <climits>
#include <cstdint>
#include <ctime>

#include "coinflipper.pb.h"
#include "coinflipper.h"

using namespace std;

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
			enable_ipv6(socket);

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
	results(results_), coins_per_second(coins_per_second_) 
	{
		previous_time = chrono::high_resolution_clock::now();
		previous_count = 0;
	};

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
			enable_ipv6(socket);

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
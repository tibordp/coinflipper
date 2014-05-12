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

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <random>
#include <mutex>
#include <sstream>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <deque>
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

using time_type = decltype(chrono::high_resolution_clock::now());
using time_difference_type = decltype(time_type() - time_type());

struct coin_push {
	uint64_t hash;
	time_type time;
	uint64_t count;

	inline bool operator<(const coin_push& other) const {
		return time < other.time;
	}
};

/* Hold the information about all the pushes in the past time interval
   Scales to up to a couple hundred thousand pushes per minute. */

#include <iostream>

class async_statistics {
	multiset<coin_push> pushes;
	time_difference_type timeout;
	map<uint64_t, uint64_t> commits_by_client; 
	mutex mtx;


	void cleanup(time_type older_than) {
		coin_push bound {0, older_than, 0};

		for (auto it = pushes.begin(); it != pushes.upper_bound(bound); ++it) {
				pushes.erase(it);
		}
	}

	// Returns a tally for a given hash
	pair<uint64_t, uint64_t> tally(bool use_hash = false, uint64_t hash = 0) 
	{
		time_type begin;
		time_type end;

		bool found_begin = false;

		uint64_t total = 0;

		for (const auto &it : pushes)
		{
			if (use_hash && (hash != it.hash))
				continue;

			if (!found_begin)
			{
				begin = it.time;
				found_begin = true;
			}
			end = it.time;

			total += it.count;
		}
		
		/* If there are some pushes from a given hash or if there are any pushes altogether */

		if (found_begin && (begin != end))
		{
			using namespace chrono;
			auto delta = end - begin;
			auto tick_count = duration_cast<milliseconds>(delta).count();	
			return make_pair(total, tick_count);
		}

		return make_pair(0, 0);
	}

	set<uint64_t> workers() {
		set<uint64_t> results;
		for (const auto& i : pushes )
			results.insert(i.hash);
		return results;
	}

public:
	async_statistics(time_difference_type timeout_) : timeout(timeout_) {};

	void push(uint64_t hash, uint64_t count) {
		lock_guard<mutex> lg(mtx);
		auto now = chrono::high_resolution_clock::now();
		
		commits_by_client[hash] += count;
		pushes.insert({hash, now, count});

		cleanup(now - timeout);
	}

	uint64_t coins_per_second() {
		lock_guard<mutex> lg(mtx);

		auto t = tally();
		
		if (t.second == 0)
			return 0;
		else
			return (double)t.first / t.second * 1000;
	}

	vector<pair<uint64_t, uint64_t>> cps_per_hash() {
		lock_guard<mutex> lg(mtx);

		vector<pair<uint64_t, uint64_t>> results;
		for (const auto& i : workers())
		{
			auto t = tally(true, i);
			if (t.second == 0)
				results.push_back(make_pair(i, 0));
			else
				results.push_back(make_pair(i, (double)t.first / t.second * 1000));
		}
		
		return results;
	}

	void clean() {
		lock_guard<mutex> lg(mtx);	
		auto now = chrono::high_resolution_clock::now();
		cleanup(now - timeout);
	}
};

class coin_listener_workers
{
	async_results& results;
	async_statistics & stats;
public:
	coin_listener_workers(async_results& results_, 
		async_statistics& stats_) : results(results_), stats(stats_) {};

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
				stats.push(cf.hash(), cf.total_flips());
			}
		}
		catch (...)
		{
			return;
		}
	}
};

/*
This class listens for client connections that request status updates.
*/

class coin_listener_clients
{
	async_results& results;
	async_statistics & stats;
public:
	coin_listener_clients(async_results& results_, 
		async_statistics& stats_) : results(results_), stats(stats_) {};

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
				cf.set_flips_per_second(stats.coins_per_second());

				rslt.first.insert_to_pb(cf);

				for (const auto &i : stats.cps_per_hash()) 
				{
					auto d = cf.add_stats();
					 d->set_hash(i.first);
					 d->set_flips_per_second(i.second);	
				}

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
	vector<thread> threads;
	async_statistics stats(chrono::seconds(10));
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


	threads.push_back(thread(coin_listener_workers(results, stats)));
	threads.push_back(thread(coin_listener_clients(results, stats)));

	// We have a thread that exports and saves status every n minutes

	threads.push_back(thread([&]
	{
		// We fetch the current status
		for (;;)
		{
			auto rslt = results.get();
			coinflipper::coinstatus cf;

			cf.set_total_flips(rslt.second);
			cf.set_flips_per_second(stats.coins_per_second());
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

			// Clean up any remaining stray connection from stats.
			stats.clean();

			this_thread::sleep_for(chrono::minutes(5));
		}
	}));

	for (auto &i : threads)
		i.join();

	return 0;
}
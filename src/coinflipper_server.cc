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
#include <cstdio>
#include <ctime>

#include "coinflipper.pb.h"
#include "coinflipper.h"

using namespace std;

/*
	This class listens for client connections that send coinflip updates.
*/

/* Don't hate :) */
using time_type = decltype(chrono::high_resolution_clock::now());
using time_difference_type = decltype(time_type() - time_type());

struct coin_push {
	uint64_t hash;
	time_type time;
	uint64_t count;
	
	/* We order the coin_pusches by time, so we can quickly remove them
	   in chronological order */
	inline bool operator<(const coin_push& other) const {
		return time < other.time;
	}
};

/* Hold the information about all the pushes in the past time interval
   Scales to up to a couple hundred thousand pushes per minute. */

#include <iostream>

struct tally_data {
	uint64_t total_coins;
	time_type begin;
	time_type end;

	bool valid() const {
		return end != begin;
	}

	uint64_t get_speed() const {
		using namespace chrono;
		if (!valid())
			return 0;
		else
			/* We convert to fractional seconds, but the result is an integer */
			return total_coins /  
			duration_cast<duration<double>>(end - begin).count();
	}

	tally_data(time_type begin_) : total_coins(0), begin(begin_) {};
};

/* RFC: Is deriving from STL containers (without overriding ctors and dtors) really a bad idea or is 
   it like ending a sentence with a preposition?

   I find that in order to simply add some additional functionality, this is way simpler than reimplementing
   the whole interface of std::map
*/

class connected_clients : public map<uint64_t, tally_data> 
{
public:
	uint64_t total_speed() {
		return accumulate(begin(), end(), 0ULL, 
			[](uint64_t a, const value_type& b)
			{
				return a + b.second.get_speed(); 
			});
	}
};

class async_statistics {
	multiset<coin_push> pushes;
	time_difference_type timeout;
	mutex mtx;

	void cleanup(time_type older_than) {
		for (auto it = pushes.begin(); it != pushes.end(); ) {
			if (it->time < older_than) {
				it = pushes.erase(it);
			} else {
				break;
			}
		}
	}

	// Returns a tally for all connected clients

	connected_clients tally() const
	{
		connected_clients clients;

		for (const auto &i : pushes)
		{		
			auto it = clients.find(i.hash);
			
			if (it == clients.end())
			{
				auto c = clients.insert(
					make_pair(i.hash, tally_data(i.time))
				);
				it = c.first;
			}

			it->second.end = i.time;
			it->second.total_coins += i.count;
		}
	 	
	 	return clients;
	}

public:
	async_statistics(time_difference_type timeout_) : timeout(timeout_) {};

	void push(uint64_t hash, uint64_t count) {
		lock_guard<mutex> lg(mtx);
		auto now = chrono::high_resolution_clock::now();
		cleanup(now - timeout);
		
		pushes.insert({hash, now, count});

	}

	/* Get a tally of coin flip speeds of all connected clients */
	connected_clients get_tally() {
		lock_guard<mutex> lg(mtx);
		auto now = chrono::high_resolution_clock::now();
		cleanup(now - timeout);

		return tally();
	}
};

/*
	This class listens for worker connections and updates the internal table of
	coin flips as well as the statistics.
*/

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

				rslt.first.insert_to_pb(cf);

				/* We also push the statistics about currently connected clients */
				auto tally = stats.get_tally();

				for (const auto &i : tally) 
				{
					auto d = cf.add_stats();
					 d->set_hash(i.first);
					 d->set_flips_per_second(i.second.get_speed());	
				}

				cf.set_flips_per_second(tally.total_speed());

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
				ofstream status("~status.cf", ios::binary);
				cf.SerializeToOstream(&status);
				
				// On posix this is atomic
				rename("~status.cf", "status.cf");
			}

			this_thread::sleep_for(chrono::seconds(1));
		}
	}));

	for (auto &i : threads)
		i.join();

	return 0;
}
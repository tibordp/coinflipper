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
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <climits>
#include <cstdint>
#include <ctime>

#include "coinflipper.pb.h"
#include "coinflipper.h"

using namespace std;

/* This function formats the number with , as a thousands separator */

template<typename T> 
string commify(T value) 
{
	struct punct : public numpunct<char>
	{
	protected:
		virtual char do_thousands_sep() const { return ','; }
		virtual string do_grouping() const { return "\03"; }
	};

	stringstream ss;
	ss.imbue({ locale(), new punct });
	ss << setprecision(0) << fixed << value;
	return ss.str();
}

/* This function formats seconds in a human readable format */

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

	vector<pair<uint64_t, uint64_t>> connected_workers;

	for (const auto& i : cf.stats())
	{
		connected_workers.push_back(make_pair(i.hash(), i.flips_per_second()));
	}

	sort(connected_workers.begin(), connected_workers.end(), [](const pair<uint64_t, uint64_t>& a, 
		const pair<uint64_t, uint64_t>& b) {return b.second < a.second;});

	if (!connected_workers.empty())
	{
		cout << "Connected clients:" << endl;
		for (const auto& i : cf.stats())
		{
			cout << hex << i.hash() << ": \t" << dec << commify(i.flips_per_second()) << " cps" <<  endl;
		}
		cout << endl;
	}

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

/*
	This function synchronously requests a status update from the server.
*/


int coin_status(const string& server_address, bool export_data = false) 
{
	zmq::socket_t socket(context, ZMQ_REQ);
	enable_ipv6(socket);

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

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
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

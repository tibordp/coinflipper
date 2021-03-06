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

#include <zmq.hpp>

#include "dummy_rng.h"

#include <iostream>
#include <string>

using namespace std;

// A global ZMQ context
zmq::context_t context(1);

void enable_ipv6(zmq::socket_t& socket)
{
#ifdef ZMQ_IPV6
	int P1 = 1;	
	socket.setsockopt(ZMQ_IPV6, &P1, sizeof(P1));
#endif
}

int coin_flipper(const string&);
int coin_server();
int coin_status(const string&, bool export_ = false);

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

#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <atomic>
#include <cstdint>
#include <array>
#include <chrono>
#include <mutex>
#include <queue>
#include <condition_variable>

#include "coinflipper.pb.h"
#include "coinfliper.hpp"

#include <zmq.hpp>

using namespace std;

void listen() {
	try {
			zmq::socket_t socket (context, ZMQ_PULL);
			socket.bind ("tcp://*:5555");

			while (true) {
				zmq::message_t update;

        	//  Wait for next request from client
				socket.recv (&update);

				coinflipper::coinbatch cf;
				cf.ParseFromArray(update.data(), update.size());

				cout << cf.hash() << endl;
			}
	} catch (...)
	{	
			return;		
	}
}

int main(){
	zmq::context_t context (1);

	bool updating = false;
	results rslt;

	vector<thread> thrds;

	thrds.push_back(thread(listen));
}
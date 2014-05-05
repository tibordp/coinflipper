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
#include "coinflipper.hpp"

#include <zmq.hpp>

using namespace std;

class socket_man {
	zmq::context_t& context;
	async_results& results;

	socket_man(zmq::context_t& context_, 
			   results& results_) : 
		context(context_),
		results(results_) {}

	void operator()() {
		try {
			zmq::socket_t socket (context, ZMQ_PULL);
			socket.bind ("tcp://*:5555");

			while (true) {
				zmq::message_t update;

        		//  Wait for next request from client
				socket.recv (&update);

				coinflipper::coinbatch cf;
				cf.ParseFromArray(update.data(), update.size());

				result_array work;
				for (auto& i : work) i = 0;

				for (auto&i : cf.coinflip())
				{
					work[i.index()] = i.flips();
				}

				results.push(work);

				cout << "Received work (id: " << hex << cf.hash() << ")" << endl;
			}
		} catch (...)
		{	
			return;		
		}
	}
};

int main(){
	zmq::context_t context (1);
	async_results results;

	vector<thread> threads;

	threads.push_back(thread(listen));

	for (auto &i : threads) i.join();
}
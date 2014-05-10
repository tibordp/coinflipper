#pragma once

#include <array>
#include <utility>
#include <mutex>
#include <cstdint>

#include <zmq.hpp>

extern int P0;
extern int P1;

// A global ZMQ context
extern zmq::context_t context;


/* 
	result_array is a simple array container that holds the statistics 
	It can be converted to any appropriate Protocol Buffers message that
	implements flips(), add_flips().
*/

class result_array : public std::array<uint64_t, 128> 
{

public:
	template <typename T> 
	void insert_to_pb(T& pb_message) const
	{
		for (int i = 0; i < 128; i ++)
		{	
			if ((*this)[i] != 0) 
			{
				auto d = pb_message.add_flips();
					 d->set_position(i);
					 d->set_flips((*this)[i]);	
			}
		}
	}

	template <typename T> 
	static result_array create_from_pb(T& pb_message)
	{
		result_array result;
		result.fill(0);

		for (const auto& i : pb_message.flips())
		{
			result[i.position()] = i.flips();
		}

		return result;
	}
};

/*
	async_results is a class that holds the current statistics about coinflips
	it is used by both the server and the worker nodes. It uses locking when 
	it updates, so it can be updated by many threads at once.
*/

class async_results 
{
private:
	result_array rslt;
	uint64_t total;
	std::mutex mtx;

public:
	async_results()
	{
		rslt.fill(0);
		total = 0;
	}

	void push(const result_array& val, uint64_t count) 
	{
		std::lock_guard<std::mutex> lg(mtx);
		total += count;

		for (int i = 0; i < 128; ++i)
		{
			rslt[i] += val[i];
		}
	}

	std::pair<result_array, uint64_t> get() 
	{
		std::lock_guard<std::mutex> lg(mtx);
		return make_pair(rslt, total);
	}

	void pop() 
	{
		std::lock_guard<std::mutex> lg(mtx);
		rslt.fill(0);
		total = 0;
	}
};
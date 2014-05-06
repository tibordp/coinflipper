#include <mutex>
#include <utility>
#include <array>

class result_array : public std::array<uint64_t, 128>
{
	/* 
		result_array can be converted to any appropriate Protocol Buffers message that
		implements flips(), add_flips()
	*/
public:

	template <typename T>
	void insert_to_pb(T& pb_message) const
	{
		int j = 0;
		for (auto i : *this)
		{
			if (i == 0) continue;
			auto d = pb_message.add_flips();
			d->set_index(j);
			d->set_flips(i);

			++j;
		}
	}
	
	template <typename T>
	static result_array create_from_pb(T& pb_message)
	{
		result_array result;
		result.fill(0);

		for (const auto& i : pb_message.flips())
		{
			result[i.index()] = i.flips();
		}

		return result;
	}
};

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

	void push(result_array& val, uint64_t count) 
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
		return std::make_pair(rslt, total);
	}

	void pop() 
	{
		std::lock_guard<std::mutex> lg(mtx);
		rslt.fill(0);
		total = 0;
	}
};
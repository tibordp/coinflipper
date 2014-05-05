#include <mutex>
#include <array>

using result_array = std::array<uint64_t, 128>;

class async_results 
{
private:
	result_array rslt;
	std::mutex mtx;

public:
	async_results()
	{
		for (auto& i : rslt) 
			i = 0;
	}

	void push(result_array& val) 
	{
		std::lock_guard<std::mutex> lg(mtx);
		for (int i = 0; i < 128; ++i)
		{
			rslt[i] += val[i];
			val[i] = 0;
		}
	}

	result_array get() 
	{
		std::lock_guard<std::mutex> lg(mtx);
		return rslt;
	}

	void pop() 
	{
		std::lock_guard<std::mutex> lg(mtx);
		for (auto& i : rslt) i = 0;
	}
};
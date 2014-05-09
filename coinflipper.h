#include <mutex>
#include <utility>
#include <array>
#include <sstream>
#include <iomanip>

/* 
	result_array can be converted to any appropriate Protocol Buffers message that
	implements flips(), add_flips()
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
		return std::make_pair(rslt, total);
	}

	void pop() 
	{
		std::lock_guard<std::mutex> lg(mtx);
		rslt.fill(0);
		total = 0;
	}
};


// This function formats the number with , as a thousands separator

template<typename T> 
std::string commify(T value) 
{
	struct punct : public std::numpunct<char>
	{
	protected:
		virtual char do_thousands_sep() const { return ','; }
		virtual std::string do_grouping() const { return "\03"; }
	};

	std::stringstream ss;
	ss.imbue({ std::locale(), new punct });
	ss << std::setprecision(0) << std::fixed << value;
	return ss.str();
}
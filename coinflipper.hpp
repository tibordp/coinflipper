using result_array = array<uint64_t, 128>;

class async_results 
{
private:
	result_array rslt;
	mutex mtx;

public:
	async_results()
	{
		for (auto& i : rslt) i = 0;
	}

	void push(result_array& val) 
	{
		lock_guard<mutex> lg(mtx);
		for (int i = 0; i < 128; ++i)
		{
			rslt[i] += val[i];
			val[i] = 0;
		}
	}

	result_array get() 
	{
		lock_guard<mutex> lg(mtx);
		return rslt;
	}

	void pop() 
	{
		lock_guard<mutex> lg(mtx);
		for (auto& i : rslt) i = 0;
	}
};
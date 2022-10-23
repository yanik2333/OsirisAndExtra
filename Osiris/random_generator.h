#pragma once
#include <random>
template <typename T>
class random_generator
{
	std::uniform_int_distribution<T> distribution;
	std::default_random_engine random_engine;
public:
	explicit random_generator(unsigned seed);
	random_generator(T min, T max, unsigned seed);
	T get() const;
	void set_range(T min, T max);
};
#include "random_generator.h"

template <typename T>
random_generator<T>::random_generator(const unsigned seed) : distribution{}, random_engine{ seed }
{
}

template <typename T>
random_generator<T>::random_generator(const T min, const T max, const unsigned seed) : distribution{ min, max }, random_engine{ seed }
{
}

template <typename T>
T random_generator<T>::get() const
{
	return distribution(random_engine);
}

template <typename T>
void random_generator<T>::set_range(T min, T max)
{
	if (distribution.min() == min && distribution.max() == max)
		return;
	distribution = std::uniform_int_distribution<T>{ min, max };
}

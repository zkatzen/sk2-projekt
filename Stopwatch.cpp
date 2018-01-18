#include "Stopwatch.hpp"

Stopwatch::Stopwatch():
    _start_time(boost::chrono::high_resolution_clock::now()) {}

Stopwatch::~Stopwatch() {}

void Stopwatch::Restart()
{
    _start_time = boost::chrono::high_resolution_clock::now();
}

std::uint64_t Stopwatch::Get_elapsed_ns()
{
    boost::chrono::nanoseconds nano_s = boost::chrono::duration_cast<boost::chrono::nanoseconds>(boost::chrono::high_resolution_clock::now() - _start_time);
    return static_cast<std::uint64_t>(nano_s.count());
}

std::uint64_t Stopwatch::Get_elapsed_us()
{
    boost::chrono::microseconds micro_s = boost::chrono::duration_cast<boost::chrono::microseconds>(boost::chrono::high_resolution_clock::now() - _start_time);
    return static_cast<std::uint64_t>(micro_s.count());
}

std::uint64_t Stopwatch::Get_elapsed_ms()
{
    boost::chrono::milliseconds milli_s = boost::chrono::duration_cast<boost::chrono::milliseconds>(boost::chrono::high_resolution_clock::now() - _start_time);
    return static_cast<std::uint64_t>(milli_s.count());
}

std::uint64_t Stopwatch::Get_elapsed_s()
{
    boost::chrono::seconds sec = boost::chrono::duration_cast<boost::chrono::seconds>(boost::chrono::high_resolution_clock::now() - _start_time);
    return static_cast<std::uint64_t>(sec.count());
}

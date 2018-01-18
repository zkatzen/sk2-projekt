#ifndef STOPWATCH_HPP
#define STOPWATCH_HPP

//Boost
#include <boost/chrono.hpp>
//Std
#include <cstdint>

class Stopwatch
{
public:
    Stopwatch();
    virtual         ~Stopwatch();
    void            Restart();
    std::uint64_t   Get_elapsed_ns();
    std::uint64_t   Get_elapsed_us();
    std::uint64_t   Get_elapsed_ms();
    std::uint64_t   Get_elapsed_s();
private:
    boost::chrono::high_resolution_clock::time_point _start_time;
};

#endif // STOPWATCH_HPP


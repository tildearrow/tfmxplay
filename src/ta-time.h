#ifndef _TA_TIME_H
#define _TA_TIME_H
#include <time.h>
#include <string>

bool operator ==(const struct timespec& l, const struct timespec& r);
bool operator >(const struct timespec& l, const struct timespec& r);
bool operator <(const struct timespec& l, const struct timespec& r);
struct timespec operator +(const struct timespec& l, const struct timespec& r);
struct timespec operator -(const struct timespec& l, const struct timespec& r);
struct timespec operator +(const struct timespec& l, const long& r);
struct timespec operator -(const struct timespec& l, const long& r);
struct timespec mkts(time_t sec, long nsec);
struct timespec stots(std::string s);
std::string tstos(struct timespec ts);
#ifndef _WIN32
struct timespec curTime(clockid_t clockSource);
#endif
#endif

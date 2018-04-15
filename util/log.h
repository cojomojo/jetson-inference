#ifndef __LOG_H__
#define __LOG_H__

#include <iostream>

#define LOG_APP_LEVEL "[imagenet-kiwirover]  "

#ifdef DEBUG
#  define LOG_ERROR(x,y) do { std::cerr << "[error]" << x << y; } while (0)
#  define LOG_WARN(x,y) do { std::cout << "[warning]" << x << y; } while (0)
#  define LOG_MSG(x,y) do { std::cout << "[message]" << x << y; } while (0)
#else
#  define LOG_ERROR(x,y) do {} while (0)
#  define LOG_WARN(x,y) do {} while (0)
#  define LOG_MSG(x,y) do {} while (0)
#endif

#endif

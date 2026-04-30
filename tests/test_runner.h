#pragma once
#include <iostream>
#include <string>

inline int _pass = 0;
inline int _fail = 0;

#define ASSERT_EQ(got, expected)                                              \
    do {                                                                       \
        auto _g = (got);                                                       \
        auto _e = (expected);                                                  \
        if (_g == _e) {                                                        \
            ++_pass;                                                           \
        } else {                                                               \
            ++_fail;                                                           \
            std::cerr << "FAIL  " << __FILE__ << ":" << __LINE__ << "\n"     \
                      << "      expected: " << _e << "\n"                     \
                      << "      got:      " << _g << "\n";                    \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(expr)  ASSERT_EQ(!!(expr), true)
#define ASSERT_FALSE(expr) ASSERT_EQ(!!(expr), false)

#define RUN_TESTS()                                                            \
    do {                                                                       \
        int total = _pass + _fail;                                             \
        std::cout << _pass << "/" << total << " tests passed\n";              \
        return _fail > 0 ? 1 : 0;                                             \
    } while (0)

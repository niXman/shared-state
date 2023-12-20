
// ----------------------------------------------------------------------------
//                              Apache License
//                        Version 2.0, January 2004
//                     http://www.apache.org/licenses/
//
// This file is part of shared-state-server(https://github.com/niXman/shared-state-server) project.
//
// This was a test task for implementing multithreaded Shared-State server using asio.
//
// Copyright (c) 2023 niXman (github dot nixman dog pm.me). All rights reserved.
// ----------------------------------------------------------------------------

#ifndef __shared_state_server__average_hpp__included
#define __shared_state_server__average_hpp__included

#include <array>
#include <numeric>
#include <cstring>

/**********************************************************************************************************************/

template<std::size_t N>
struct average {
    average()
        :m_arr{{}}
    {}

    void update(std::size_t i) {
        std::memmove(m_arr.begin(), m_arr.begin() + 1, sizeof(std::size_t) * N-1);
        m_arr.back() = i;
    }

    std::size_t avg() const {
        std::size_t res = std::accumulate(m_arr.begin(), m_arr.end(), 0ul);
        return res / N;
    }

private:
    std::array<std::size_t, N> m_arr;
};

/**********************************************************************************************************************/

#endif // __shared_state_server__average_hpp__included

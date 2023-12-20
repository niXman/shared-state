
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

#ifndef __shared_state_server__fnv1a_hpp__included
#define __shared_state_server__fnv1a_hpp__included

#include <string_view>
#include <cstdint>

/**********************************************************************************************************************/

constexpr std::uint32_t fnv1a(std::string_view str) {
    std::uint32_t seed = 0x811c9dc5;
    for ( const auto *it = str.begin(); it != str.end(); ++it ) {
        seed = (std::uint32_t)((seed ^ ((std::uint32_t)*it)) * ((std::uint64_t)0x01000193));
    }

    return seed;
}

/**********************************************************************************************************************/

#endif // __shared_state_server__fnv1a_hpp__included

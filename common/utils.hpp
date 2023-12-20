
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

#ifndef __shared_state_server__utils_hpp__included
#define __shared_state_server__utils_hpp__included

#include <chrono>

#include <boost/asio.hpp>

namespace ba = boost::asio;
namespace bs = boost::system;
using tcp = boost::asio::ip::tcp;

/**********************************************************************************************************************/
// error processing

struct error_info {
    const char *descr;
    const char *file;
    int line;
    const char *func;
    int ecode;
    std::string emsg;
};

inline std::ostream& operator<< (std::ostream &os, const error_info &ei) {
    os << ei.descr << ": " << ei.file << "(" << ei.line << "): " << ei.func
       << ", error: " << ei.emsg << " (" << ei.ecode << ")"
    << std::flush;

    return os;
}

inline error_info make_error_info(
     const char *descr
    ,const char *file
    ,int line
    ,const char *func
    ,int ec
    ,const char *emsg)
{
    return {descr, file, line, func, ec, emsg};
}

inline error_info make_error_info(
     const char *descr
    ,const char *file
    ,int line
    ,const char *func
    ,const bs::error_code &ec)
{
    return {descr, file, line, func, ec.value(), ec.message()};
}

#define MAKE_ERROR_INFO(descr, ec) make_error_info(descr, __FILE__, __LINE__, __func__, ec)
#define MAKE_ERROR_INFO_2(descr, ec, emsg) make_error_info(descr, __FILE__, __LINE__, __func__, ec, emsg)

#define CALL_ERROR_HANDLER(cb, ei) cb(ei)

/**********************************************************************************************************************/

inline std::uint64_t ms_time() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/**********************************************************************************************************************/

#endif // __shared_state_server__utils_hpp__included

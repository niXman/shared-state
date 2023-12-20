
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

#ifndef __shared_state_server__string_buffer_hpp__included
#define __shared_state_server__string_buffer_hpp__included

#include "intrusive_base.hpp"
#include "intrusive_ptr.hpp"
#include "object_pool.hpp"

#include <string>
#include <type_traits>

/**********************************************************************************************************************/

struct string_buffer: intrusive_base<string_buffer> {
    string_buffer()
        :m_buf{}
    {}

    template<
         typename Arg
        ,typename = typename std::enable_if_t<
            std::is_same_v<typename std::decay_t<Arg>, string_buffer>
        >
    >
    string_buffer(Arg &&arg)
        :m_buf{std::forward<Arg>(arg).m_buf}
    {}

    template<
         typename Arg0
        ,typename ...Args
        ,typename = typename std::enable_if_t<
            !std::is_same_v<typename std::decay_t<Arg0>, string_buffer>
        >
    >
    string_buffer(Arg0 &&arg0, Args && ...args)
        :m_buf{std::forward<Arg0>(arg0), std::forward<Args>(args)...}
    {}

    const auto& string() const noexcept { return m_buf; }
    auto&       string()       noexcept { return m_buf; }
    const char* data()   const noexcept { return m_buf.data(); }
    std::size_t size()   const noexcept { return m_buf.size(); }
    std::size_t length() const noexcept { return m_buf.length(); }

    string_buffer& preppend(const char *str) { m_buf.insert(0, str); return *this; }
    string_buffer& append(char ch) noexcept { m_buf += ch; return *this; }
    string_buffer& append(const std::string &str) noexcept { m_buf += str; return *this; }

    void pop_back() noexcept { m_buf.pop_back(); }
    void erase(std::size_t pos, std::size_t n) { m_buf.erase(pos, n); }
    void clear() noexcept { m_buf.clear(); }

    string_buffer& operator=  (const char *str) { m_buf = str; return *this; }
    string_buffer& operator+= (const char chr) { m_buf += chr; return *this; }
    string_buffer& operator+= (const char *str) { m_buf += str; return *this; }
    string_buffer& operator+= (const string_buffer &str) { m_buf += str.m_buf; return *this; }

    friend inline bool operator== (const string_buffer &l, const string_buffer &r) noexcept
    { return l.m_buf == r.m_buf; }

    friend inline bool operator!= (const string_buffer &l, const string_buffer &r) noexcept
    { return l.m_buf != r.m_buf; }

    friend inline bool operator< (const string_buffer &l, const string_buffer &r) noexcept
    { return l.m_buf < r.m_buf; }

private:
    std::string m_buf;
};

/**********************************************************************************************************************/

using buffers_pool = object_pool<string_buffer>;

using shared_buffer = intrusive_ptr<string_buffer>;

template<typename ...Args>
inline auto make_buffer(buffers_pool &pool, Args && ...args)
{ return pool.get_del([](string_buffer *s){ s->clear(); }, std::forward<Args>(args)...); }

/**********************************************************************************************************************/

#endif // __shared_state_server__string_buffer_hpp__included

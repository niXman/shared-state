
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

#ifndef __shared_state_server__session_manager_hpp__included
#define __shared_state_server__session_manager_hpp__included

#include "utils.hpp"
#include "string_buffer.hpp"
#include "session.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/intrusive/list.hpp>

/**********************************************************************************************************************/

struct session_manager {
    using session_ptr = session::session_ptr;

    session_manager(const session_manager &) = delete;
    session_manager& operator= (const session_manager &) = delete;
    session_manager(session_manager &&) = delete;
    session_manager& operator= (session_manager &&) = delete;

    session_manager(
         ba::io_context &ioctx
        ,std::size_t max_size
        ,std::size_t inactivity_time
        ,sessions_pool &ses_pool
        ,buffers_pool &str_pool
    )
        :m_strand{ioctx}
        ,m_max_size{max_size}
        ,m_inactivity_time{inactivity_time}
        ,m_ses_pool{ses_pool}
        ,m_str_pool{str_pool}
        ,m_list{}
    {}

    auto create(tcp::socket sock) {
        auto sptr = m_ses_pool.get_del(
             [this](session *s){ session_deleter(s); }
            ,std::move(sock)
            ,m_max_size
            ,m_inactivity_time
            ,m_str_pool
        );

        session *raw_ptr = sptr.get();
        ba::post(
             m_strand
            ,[this, raw_ptr]
             ()
             { m_list.push_back(*raw_ptr); }
        );

        return sptr;
    }

    // will close all the sessions
    auto reset() {
        return ba::post(
             m_strand
            ,ba::use_future([this](){
                for ( auto &it: m_list ) { it.stop(); }
            })
        );
    }

    template<typename ErrorCB>
    void broadcast(shared_buffer msg, bool disconnect, ErrorCB error_cb, session_ptr holder) {
        ba::post(
             m_strand
            ,ba::use_future(
                [this, msg=std::move(msg), disconnect, error_cb=std::move(error_cb), holder=std::move(holder)]
                () mutable
                { broadcast_impl(std::move(msg), disconnect, std::move(error_cb), std::move(holder)); }
            )
        );
    }

    std::size_t size() const {
        auto fut = ba::post(
             m_strand
            ,ba::use_future([this](){ return m_list.size(); })
        );

        return fut.get();
    }

private:
    template<typename ErrorCB>
    void broadcast_impl(shared_buffer msg, bool disconnect, ErrorCB error_cb, session_ptr holder) {
        for ( auto it = m_list.begin(); it != m_list.end(); ++it ) {
            if ( std::addressof(*it) != holder.get() ) {
                it->send(
                     [](bool){}
                    ,error_cb
                    ,msg
                    ,disconnect
                    ,holder
                );
            }
        }
    }

    void session_deleter(session *s) {
        s->stop();

        ba::post(
             m_strand
            ,[this, s](){
                auto it = m_list.iterator_to(*s);
                m_list.erase(it);
                s->~session();
            }
        );
    }

    ba::io_context::strand m_strand;
    std::size_t m_max_size;
    std::size_t m_inactivity_time;
    sessions_pool &m_ses_pool;
    buffers_pool &m_str_pool;
    boost::intrusive::list<session> m_list;
};

/**********************************************************************************************************************/

#endif // __shared_state_server__session_manager_hpp__included

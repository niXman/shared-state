
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

#ifndef __shared_state_server__acceptor_hpp__included
#define __shared_state_server__acceptor_hpp__included

#include "utils.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

/**********************************************************************************************************************/

struct acceptor {
    acceptor(const acceptor &) = delete;
    acceptor& operator= (const acceptor &) = delete;
    acceptor(acceptor &&) = delete;
    acceptor& operator= (acceptor &&) = delete;

    acceptor(ba::io_context &ioctx, const std::string &ip, std::uint16_t port)
        :m_ioctx{ioctx}
        ,m_acc{ba::make_strand(ioctx)}
        ,m_endpoint{ba::ip::make_address(ip), port}
    {}

    // OnAcceptedCB's signature: void(tcp::socket)
    // ErrorCB's signature: void(error_handler_info)
    template<typename OnAcceptedCB, typename ErrorCB>
    void start(OnAcceptedCB on_accepted_cb, ErrorCB error_cb) {
        ba::dispatch(
             m_acc.get_executor()
            ,[this, on_accepted_cb=std::move(on_accepted_cb), error_cb=std::move(error_cb)]
             () mutable
             { start_impl(std::move(on_accepted_cb), std::move(error_cb)); }
        );
    }

    auto stop() {
        return ba::post(
             m_acc.get_executor()
            ,ba::use_future([this]() {
                bs::error_code ec;
                m_acc.close(ec);
            })
        );
    }
    auto is_open() {
        return ba::post(
             m_acc.get_executor()
            ,ba::use_future([this](){
                return m_acc.is_open();
            })
        );
    }

private:
    template<typename OnAcceptedCB, typename ErrorCB>
    void start_impl(OnAcceptedCB on_accepted_cb, ErrorCB error_cb) {
        m_acc.open(m_endpoint.protocol());
        m_acc.set_option(tcp::acceptor::reuse_address{true});
        m_acc.bind(m_endpoint);
        m_acc.listen();

        start_accept(std::move(on_accepted_cb), std::move(error_cb));
    }
    template<typename OnAcceptedCB, typename ErrorCB>
    void start_accept(OnAcceptedCB on_accepted_cb, ErrorCB error_cb) {
        m_acc.async_accept(
             ba::make_strand(m_ioctx)
            ,[this, on_accepted_cb=std::move(on_accepted_cb), error_cb=std::move(error_cb)]
             (const bs::error_code &ec, tcp::socket sock) mutable
             { on_accepted(std::move(on_accepted_cb), std::move(error_cb), ec, std::move(sock)); }
        );
    }
    template<typename OnAcceptedCB, typename ErrorCB>
    void on_accepted(OnAcceptedCB on_accepted_cb, ErrorCB error_cb, const bs::error_code &ec, tcp::socket sock) {
        if ( ec ) {
            if ( ec == ba::error::operation_aborted ) {
                return;
            }

            CALL_ERROR_HANDLER(error_cb, MAKE_ERROR_INFO("acceptor", ec));

            return;
        }

        on_accepted_cb(std::move(sock));

        start_accept(std::move(on_accepted_cb), std::move(error_cb));
    }

private:
    ba::io_context &m_ioctx;
    tcp::acceptor m_acc;
    const tcp::endpoint m_endpoint;
};

/**********************************************************************************************************************/

#endif // __shared_state_server__acceptor_hpp__included

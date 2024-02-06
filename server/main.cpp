
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

#include "cmdargs/cmdargs.hpp"

#include <iostream>

#include "../common/fnv1a.hpp"
#include "../common/utils.hpp"
#include "../common/intrusive_ptr.hpp"
#include "../common/object_pool.hpp"
#include "../common/string_buffer.hpp"
#include "../common/state_storage.hpp"
#include "../common/session.hpp"
#include "../common/session_manager.hpp"
#include "../common/acceptor.hpp"

#include <thread>
#include <vector>

#ifndef HIDE_DEBUG_OUTPUT
#   define DEBUG_EXPR(...) __VA_ARGS__
#else
#   define DEBUG_EXPR(...)
#endif

using session_ptr = session::session_ptr;

/**********************************************************************************************************************/

// PING - is sent only by the client to the server,
//        in the form "PING ms-time\n".
//        the server just sends these messages back to the client.

// DATA - is sent both by the client to the server and by the server to the client
//        in the form "DATA key val\n".

// STOP - is sent by the server to clients in form "STOP \n", telling them that they should
//        disconnect and reconnect later because the server will reset its state.

static constexpr auto ALL_CMDS_LEN = 4u;

static constexpr auto PING_CMD = std::string_view{"PING"};
static_assert(PING_CMD.size() == ALL_CMDS_LEN);
static constexpr auto PING_HASH = fnv1a(PING_CMD);

static constexpr auto DATA_CMD = std::string_view{"DATA"};
static_assert(DATA_CMD.size() == ALL_CMDS_LEN);
static constexpr auto DATA_HASH = fnv1a(DATA_CMD);

/**********************************************************************************************************************/

void error_handler(const error_info &ei) {
    std::cerr << "error_handler> " << ei << std::endl;
}

/**********************************************************************************************************************/
// called on socket strand
// PING

template<typename ErrorCB>
bool handle_ping(const ErrorCB &error_cb, shared_buffer buf, session_ptr session) {
    auto *session_ptr = session.get();
    session_ptr->send(
         [](bool){}
        ,error_cb
        ,std::move(buf)
        ,false
        ,std::move(session)
    );

    return true;
}

/**********************************************************************************************************************/
// called on socket strand
// DATA

template<typename ErrorCB>
bool handle_data(
     const ErrorCB &error_cb
    ,state_storage &state
    ,session_manager &smgr
    ,shared_buffer buf
    ,session_ptr session)
{
    const auto data = std::string_view{buf->data() + (4 + 1), buf->size() - (4 + 1)}; // 1 - because of space char
    const auto pos  = data.find(' ');
    if ( pos != std::string_view::npos ) {
        const auto key = data.substr(0, pos);
        const auto val = data.substr(pos+1);
        state.update(
             key
            ,val
            ,std::move(buf)
            ,[error_cb=std::move(error_cb), &smgr, session=std::move(session)]
             (shared_buffer buf) mutable
             { smgr.broadcast(std::move(buf), false, std::move(error_cb), std::move(session)); }
        );

        return true;
    }

    return false;
}

/**********************************************************************************************************************/
// called on socket strand

template<typename ErrorCB>
bool on_readed(
     state_storage &state
    ,session_manager &smgr
    ,const ErrorCB &error_cb
    ,shared_buffer buf
    ,session_ptr session)
{
    if ( buf->size() > ALL_CMDS_LEN && *(buf->data() + ALL_CMDS_LEN) == ' ' ) {
        std::string_view cmd{buf->data(), ALL_CMDS_LEN};
        switch ( auto hash = fnv1a(cmd); hash ) {
            case PING_HASH: { return handle_ping(error_cb, std::move(buf), std::move(session)); }
            case DATA_HASH: { return handle_data(error_cb, state, smgr, std::move(buf), std::move(session)); }
            default: { CALL_ERROR_HANDLER(error_cb, MAKE_ERROR_INFO_2("on_readed", -1, "wrong line received!")); return false; }
        }
    }

    return false;
}

/**********************************************************************************************************************/
// called on socket's strand

template<typename Iter>
void sync_next(state_storage &state, Iter prev, session_ptr session) {
    auto [latest, iter, buf] = state.get_next(std::move(prev));
    if ( !latest ) {
        auto *session_ptr = session.get();
        auto session2 = session;
        session_ptr->send(
            [&state, iter=std::move(iter), session=std::move(session)]
             (bool sent)
             { if ( sent ) sync_next(state, std::move(iter), std::move(session)); }
            ,error_handler
            ,std::move(buf)
            ,false
            ,std::move(session2)
        );
    }
}

// called on acceptor strand
void start_sync(state_storage &state, session_ptr session) {
    auto [latest, iter, buf] = state.get_first();
    if ( !latest ) {
        auto *session_ptr = session.get();
        auto session2 = session;
        session_ptr->send(
            [&state, iter=std::move(iter), session=std::move(session)]
             (bool sent)
             { if ( sent ) sync_next(state, std::move(iter), std::move(session)); }
            ,error_handler
            ,std::move(buf)
            ,false
            ,std::move(session2)
        );
    }
}

/**********************************************************************************************************************/

// called on acceptor strand
void on_new_connection(state_storage &state, session_manager &smgr, tcp::socket sock) {
    auto ep = sock.remote_endpoint();
    auto addr = ep.address().to_string();
    addr += ":";
    addr += std::to_string(ep.port());

    auto size_fut = state.size();
    std::cout << "new connection from: " << addr
              << ", will send " << size_fut.get() << " pairs..." << std::endl;

    auto session = smgr.create(std::move(sock));
    session->start(
         [&state, &smgr]
         (shared_buffer buf, session_ptr session)
         { return on_readed(state, smgr, error_handler, std::move(buf), std::move(session)); }
        ,error_handler
        ,session
    );

    start_sync(state, std::move(session));
}

/**********************************************************************************************************************/

void start_statistics_timer(
     ba::io_context &ioctx
    ,buffers_pool &bufs
    ,sessions_pool &ses
    ,session_manager &smgr
    ,std::unique_ptr<ba::steady_timer> timer = {})
{
    timer = (!timer) ? std::make_unique<ba::steady_timer>(ioctx) : std::move(timer);

    auto *timer_ptr = timer.get();
    timer_ptr->expires_from_now(std::chrono::seconds(1));
    timer_ptr->async_wait(
        [&ioctx, &bufs, &ses, &smgr, timer=std::move(timer)]
        (bs::error_code) mutable
    {
        std::cout
            << "buffers  in use   : " << bufs.in_use() << std::endl
            << "sessions in use   : " << ses.in_use() << std::endl
            << "active connections: " << smgr.size() << std::endl
            << "===============================" << std::endl
        ;
        start_statistics_timer(ioctx, bufs, ses, smgr, std::move(timer));
    });
}

/**********************************************************************************************************************/

void start_signal_handler(
     ba::io_context &ioctx
    ,acceptor &acc
    ,session_manager &smgr
    ,state_storage &state
    ,std::unique_ptr<ba::signal_set> signals = {})
{
    if ( !signals ) {
        signals = std::make_unique<ba::signal_set>(ioctx, SIGINT, SIGTERM, SIGUSR1);
        signals->add(SIGUSR2);
    }

    auto *signals_ptr = signals.get();
    signals_ptr->async_wait(
        [&ioctx, &acc, &smgr, &state, signals=std::move(signals)]
        (bs::error_code, int sig) mutable {
            std::cout << "SIG" << sigabbrev_np(sig) << " signal received!" << std::endl;
            if ( sig == SIGINT || sig == SIGTERM ) {
                ioctx.stop();
            } else {
                if ( sig == SIGUSR1 ) {
                    auto is_open_fut = acc.is_open();
                    if ( is_open_fut.get() ) {
                        std::cout << "stop accept!" << std::endl;
                        acc.stop();
                    } else {
                        std::cout << "start accept!" << std::endl;
                        acc.start(
                             [&state, &smgr] (tcp::socket sock)
                             { on_new_connection(state, smgr, std::move(sock)); }
                            ,error_handler
                        );
                    }
                } else if ( sig == SIGUSR2 ) {
                    auto acc_fut = acc.stop();
                    acc_fut.get();

                    auto smgr_fut = smgr.reset();
                    smgr_fut.get();

                    auto reset_fut = state.reset();
                    reset_fut.get();

                    acc.start(
                         [&state, &smgr] (tcp::socket sock)
                         { on_new_connection(state, smgr, std::move(sock)); }
                        ,error_handler
                    );
                }

                start_signal_handler(
                     ioctx
                    ,acc
                    ,smgr
                    ,state
                    ,std::move(signals)
                );
            }
        }
    );
}

/**********************************************************************************************************************/

int main(int argc, char **argv) try {
    struct: cmdargs::kwords_group {
        CMDARGS_OPTION_ADD(ip, std::string, "server IP", and_(port));
        CMDARGS_OPTION_ADD(port, std::uint16_t, "server PORT", and_(ip));
        CMDARGS_OPTION_ADD(threads, std::size_t
            ,"the number of working threads, by default - all avail threads be used"
            ,optional, default_<std::size_t>(std::thread::hardware_concurrency()));
        CMDARGS_OPTION_ADD(max_size, std::size_t, "the maximum length of input lines"
            ,optional, default_<std::size_t>(1024u));
        CMDARGS_OPTION_ADD(sessions_n, std::size_t, "the number of initialy preallocated sessions"
            ,optional, default_<std::size_t>(1024u));
        CMDARGS_OPTION_ADD(buffers_n, std::size_t, "the number of initialy preallocated string buffers"
            ,optional, default_<std::size_t>(1024u*10u));
        CMDARGS_OPTION_ADD(inactivity_time, std::size_t
            ,"the timeout in MS after which a client will be disconnected as dead, or 0 to disable"
            ,optional, default_<std::size_t>(1000u));

        CMDARGS_OPTION_ADD_HELP();
        CMDARGS_OPTION_ADD_VERSION("0.0.1");
    } const kwords;

    // command line processing
    std::string error;
    const auto args = cmdargs::parse_args(&error, argc, argv, kwords);
    if ( !error.empty() ) {
        std::cerr << "command line error: " << error << std::endl;
        return EXIT_FAILURE;
    }
    if ( cmdargs::is_help_or_version_requested(std::cout, argv[0], args) ) {
        return EXIT_SUCCESS;
    }
    const auto ip         = args[kwords.ip];
    const auto port       = args[kwords.port];
    const auto threads    = args[kwords.threads];
    const auto sessions_n = args[kwords.sessions_n];
    const auto buffers_n  = args[kwords.buffers_n];
    const auto ina_time   = args[kwords.inactivity_time];
    const auto max_size   = args[kwords.max_size];

    ba::io_context ioctx(threads);
    ioctx.post([threads]{ std::cout << "server started with " << threads << " threads..." << std::endl; });

    buffers_pool str_pool{buffers_n};
    sessions_pool ses_pool{sessions_n};
    state_storage state{ioctx};
    session_manager smgr{ioctx, max_size, ina_time, ses_pool, str_pool};
    acceptor acc{ioctx, ip, port};
    acc.start(
         [&state, &smgr] (tcp::socket sock)
         { on_new_connection(state, smgr, std::move(sock)); }
        ,error_handler
    );

    // for statistic
    start_statistics_timer(ioctx, str_pool, ses_pool, smgr);

    // LINUX signal handler
    start_signal_handler(ioctx, acc, smgr, state);

    std::vector<std::thread> threadsv;
    threadsv.reserve(threads);
    for ( auto n = threads-1; n; --n ) {
        threadsv.emplace_back([&ioctx]{ ioctx.run(); });
    }

    // we will blocked here until SIGINT/SIGTERM
    ioctx.run();

    // wait for all threads to exit
    for ( auto &it: threadsv ) {
        it.join();
    }

    std::cout << "server stopped!" << std::endl;

    return EXIT_SUCCESS;
} catch (const std::exception &ex) {
    std::cerr << "std::exception: " << ex.what() << std::endl;

    return EXIT_FAILURE;
}

/**********************************************************************************************************************/

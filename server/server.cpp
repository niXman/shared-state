
// ----------------------------------------------------------------------------
//                              Apache License
//                        Version 2.0, January 2004
//                     http://www.apache.org/licenses/
//
// This file is part of Shared-State(https://github.com/niXman/shared-state) project.
//
// This was a test task for implementing multithreaded Shared-State server using asio.
//
// Copyright (c) 2023 niXman (github dot nixman dog pm.me). All rights reserved.
// ----------------------------------------------------------------------------

#include "cmdargs/cmdargs.hpp"

#include "../common/utils.hpp"
#include "../common/value_hasher.hpp"
#include "../common/state_storage.hpp"
#include "../common/session.hpp"
#include "../common/session_manager.hpp"
#include "../common/acceptor.hpp"

#include <iostream>
#include <thread>
#include <vector>
#include <map>

#ifndef HIDE_DEBUG_OUTPUT
#   define DEBUG_EXPR(...) __VA_ARGS__
#else
#   define DEBUG_EXPR(...)
#endif

/**********************************************************************************************************************/
// called on socket strand

template<typename ErrorCB>
void handle_ping(ErrorCB error_cb, shared_buffer val, holder_ptr holder) {
    auto &socket = holder->get_socket();
    ba::post(
         socket
        ,[error_cb=std::move(error_cb), val=std::move(val), holder=std::move(holder)]
         () mutable {
            auto sptr = holder.get();
            sptr->send([](){}, std::move(error_cb), std::move(val), std::move(holder));
        }
    );
}

template<typename ErrorCB>
void handle_data(session_manager &smgr, ErrorCB error_cb, shared_buffer val, holder_ptr holder) {

}

template<typename ErrorCB>
void handle_sync(state_storage &state, ErrorCB error_cb, shared_buffer val, holder_ptr holder) {

}

template<typename ErrorCB>
void on_readed(state_storage &state, session_manager &smgr, ErrorCB error_cb, shared_buffer buf, holder_ptr holder) {
    bool ok = handle_incomming(
         error_cb
        ,std::move(buf)
        ,[](auto error_cb, shared_buffer val, holder_ptr holder)
         { handle_ping(std::move(error_cb), std::move(val), std::move(holder)); } // PING
        ,[&state](auto error_cb, shared_buffer val, holder_ptr holder)
         { handle_sync(state, std::move(error_cb), std::move(val), std::move(holder)); } // SYNC
        ,[&smgr](auto error_cb, shared_buffer val, holder_ptr holder)
         { handle_data(smgr, std::move(error_cb), std::move(val), std::move(holder)); } // DATA
        ,holder
    );
    if ( !ok ) {
        CALL_ERROR_HANDLER(error_cb, bs::error_code{});

        return;
    }

    smgr.broadcast(std::move(buf));
}

// called on acceptor strand
void on_new_connection(state_storage &state, session_manager &smgr, tcp::socket sock) {
    auto ep = sock.remote_endpoint();
    auto error_cb = [ep](const char *file, int line, const char *func, bs::error_code ec) {
        if ( ec ) {
            if ( ec == ba::error::eof ) {
                std::cerr
                    << "the client("
                    << ep.address().to_string() << ":" << ep.port()
                    << ") disconnected"
                    << std::endl;
            } else {
                FORMAT_ERROR_MESSAGE(std::cerr, file, line, func, ec);
            }
        }
    };

    auto size_fut = state.get_size();
    std::cout << "new connection from: " << ep.address().to_string() << ", will send " << size_fut.get() << " pairs..." << std::endl;

    auto sptr = smgr.create(std::move(sock));
    sptr->start(
         [&state, &smgr, &error_cb]
         (shared_buffer buf, holder_ptr holder)
        { on_readed(state, smgr, error_cb, std::move(buf), std::move(holder)); }
        ,error_cb
        ,sptr
    );

    for ( auto it = state.get_first().get(); !it.latest; it = state.get_next(it).get() ) {
        std::cout << "key=" << it.key_hash << std::endl;
    }
}

/**********************************************************************************************************************/

int main(int argc, char **argv) try {
    struct: cmdargs::kwords_group {
        CMDARGS_OPTION_ADD(ip, std::string, "server IP", and_(port));
        CMDARGS_OPTION_ADD(port, std::uint16_t, "server PORT", and_(ip));
        CMDARGS_OPTION_ADD(threads, std::size_t, "the number of working threads"
            ,optional, default_<std::size_t>(2u));
        CMDARGS_OPTION_ADD(recalculate, std::size_t, "the number of recalculations of SHA1"
            ,optional, default_<std::size_t>(1u));

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
    const auto ip     = args.get(kwords.ip);
    const auto port   = args.get(kwords.port);
    const auto threads= args.get(kwords.threads);
    const auto recalc = args.get(kwords.recalculate);

    ba::io_context ioctx(threads);
    ioctx.post([threads]{ std::cout << "server started with " << threads << " threads..." << std::endl; });

    value_hasher hasher{ioctx, recalc};
    state_storage state{ioctx, hasher};
    session_manager smgr{ioctx};
    acceptor acc{ioctx, ip, port};
    acc.start(
         [&state, &smgr](tcp::socket sock){ on_new_connection(state, smgr, std::move(sock)); }
        ,[]
         (const char *file, int line, const char *func, bs::error_code ec)
         { FORMAT_ERROR_MESSAGE(std::cerr, file, line, func, ec); }
    );

    ba::signal_set signals{ioctx, SIGINT, SIGTERM};
    signals.async_wait([&ioctx](const bs::error_code &, int) {
        std::cout << "SIGINT/SIGTERM received!" << std::endl;

        ioctx.stop();
    });

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

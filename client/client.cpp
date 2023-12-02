
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

#include <boost/asio.hpp>

namespace ba = boost::asio;
namespace bs = boost::system;
using tcp = boost::asio::ip::tcp;

#include <iostream>

/**********************************************************************************************************************/

struct client {
    client(const client &) = delete;
    client& operator= (const client &) = delete;

    client(ba::io_context &ioctx, const std::string &ip, std::uint16_t port, const std::string &fname, std::size_t ping)
        :m_socket{ioctx}
        ,m_ip{ip}
        ,m_port{port}
        ,m_state_fname{fname}
        ,m_ping{ping}
        ,m_ping_timer{ioctx}
    {}

    ~client()
    {}

    // CB's signature when specified: void(error_code)
    template<typename ...CB>
    void start(CB && ...cb) {
        static_assert(sizeof...(cb) == 0 || sizeof...(cb) == 1);
        if constexpr ( sizeof...(cb) == 0 ) {
            return start_impl([](const bs::error_code &){});
        } else {
            return start_impl(std::move(cb)...);
        }
    }

    void send(std::string str) {
        ba::post(
             m_socket.get_executor()
            ,[this, str=std::move(str)]
             () mutable
             { send_impl(std::move(str)); }
        );
    }

private:
    template<typename CB>
    void start_impl(CB cb) {
        tcp::endpoint ep{ba::ip::make_address_v4(m_ip), m_port};
        m_socket.async_connect(
             ep
            ,[this, cb=std::move(cb)]
             (const bs::error_code &ec) mutable
             { on_connected(ec, std::move(cb)); }
        );
    }
    template<typename CB>
    void on_connected(const bs::error_code &ec, CB cb) {
        cb(ec);
        if ( ec ) {
            return;
        }

        start_read();
    }
    void start_read() {
        ba::async_read_until(
             m_socket
            ,ba::dynamic_buffer(m_buf)
            ,'\n'
            ,[this]
             (const bs::error_code &ec, std::size_t rd)
             { on_readed(ec, rd); }
        );
    }
    void on_readed(const bs::error_code &ec, std::size_t rd) {
        if ( ec ) {
            std::cerr << "read error: " << ec << std::endl;

            return;
        }

        auto str = m_buf.substr(0, rd);
        m_buf.erase(0, rd);

        std::cout << "readed: " << str << std::flush;
    }

    void send_impl(std::string str) {
        //std::cout << "send_impl" << std::endl;
        const auto *ptr = str.data();
        const auto size = str.size();
        ba::async_write(
             m_socket
            ,ba::buffer(ptr, size)
            ,[this, str=std::move(str)]
             (const bs::error_code &ec, std::size_t wr)
             { on_sent(ec, wr); }
        );
    }
    void on_sent(const bs::error_code &ec, std::size_t) {
        //std::cout << "on_sent" << std::endl;
        if ( ec ) {
            std::cerr  << "send error: " << ec << std::endl;
        }
    }

private:
    tcp::socket m_socket;
    std::string m_buf;
    std::string m_ip;
    std::uint16_t m_port;
    std::string m_state_fname;
    std::size_t m_ping;
    ba::steady_timer m_ping_timer;
};

/**********************************************************************************************************************/

struct term_reader {
    term_reader(const term_reader &) = delete;
    term_reader& operator= (const term_reader &) = delete;

    term_reader(ba::io_context &ioctx)
        :m_ioctx{ioctx}
        ,m_stdin{m_ioctx, ::dup(STDIN_FILENO)}
    {}

    // CB's signature: void(error_code, std::string)
    template<typename CB>
    void start(CB cb) {
        assert(m_stdin.is_open());

        ba::post(
             m_stdin.get_executor()
            ,[this, cb=std::move(cb)]
             () mutable
             { read(std::move(cb)); }
        );
    }

private:
    template<typename CB>
    void read(CB cb) {
        ba::async_read_until(
             m_stdin
            ,ba::dynamic_buffer(m_buf)
            ,'\n'
            ,[this, cb=std::move(cb)]
             (const bs::error_code &ec, std::size_t rd) mutable
             { readed(ec, rd, std::move(cb)); }
        );
    }
    template<typename CB>
    void readed(const bs::error_code &ec, std::size_t rd, CB cb) {
        if ( ec ) {
            cb(ec, std::string{});
        } else {
            auto str = m_buf.substr(0, rd);
            m_buf.erase(0, rd);

            if ( str == "exit\n" ) {
                m_ioctx.stop();

                return;
            }

            cb(ec, std::move(str));

            read(std::move(cb));
        }
    }

private:
    ba::io_context &m_ioctx;
    ba::posix::stream_descriptor m_stdin;
    std::string m_buf;
};

/**********************************************************************************************************************/

struct: cmdargs::kwords_group {
    CMDARGS_OPTION_ADD(ip, std::string, "server IP", and_(port));
    CMDARGS_OPTION_ADD(port, std::uint16_t, "server PORT", and_(ip));
    CMDARGS_OPTION_ADD(fname, std::string, "the state file name (not used if not specified)", optional);
    CMDARGS_OPTION_ADD(ping, std::size_t, "ping interval in seconds (not used if not specified)", optional);

    CMDARGS_OPTION_ADD_HELP();
} static const kwords;

/**********************************************************************************************************************/

int main(int argc, char **argv) try {
    // command line processing
    std::string error;
    const auto args = cmdargs::parse_args(&error, argc, argv, kwords);
    if ( !error.empty() ) {
        std::cerr << "command line error: " << error << std::endl;

        return EXIT_FAILURE;
    }
    if ( args.is_set(kwords.help) ) {
        cmdargs::show_help(std::cout, argv[0], args);

        return EXIT_SUCCESS;
    }

    const auto &ip    = args.get(kwords.ip);
    const auto port   = args.get(kwords.port);
    const auto &fname = args.is_set(kwords.fname) ? args.get(kwords.fname) : std::string{};
    const auto ping   = args.is_set(kwords.ping) ? args.get(kwords.ping) : 0;

    // io_context + client
    ba::io_context ioctx;
    client cli{ioctx, ip, port, fname, ping};
    cli.start(
        [](const bs::error_code &ec) {
            if ( !ec ) {
                std::cout << "successfully connected!" << std::endl;
            } else {
                std::cout << "connection error: " << ec << std::endl;
            }
        }
    );

    // for reading `stdin` asynchronously
    term_reader term{ioctx};
    term.start(
        [&cli](const bs::error_code &ec, std::string str){
            if ( ec ) {
                std::cout << "term: read error: " << ec << std::endl;
            } else {
                //std::cout << "term: str=" << str << std::endl;
                cli.send(std::move(str));
            }
        }
    );

    // run
    ioctx.run();

    return EXIT_SUCCESS;
} catch (const std::exception &ex) {
    std::cerr << "std::exception: " << ex.what() << std::endl;

    return EXIT_FAILURE;
}

/**********************************************************************************************************************/

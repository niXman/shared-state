
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
#include <queue>

using shared_buffer = std::shared_ptr<std::string>;

shared_buffer make_buffer(std::string str = {}) {
    return std::make_shared<shared_buffer::element_type>(std::move(str));
}

shared_buffer make_buffer(const char *beg, const char *end) {
    return make_buffer(std::string{beg, end});
}

/**********************************************************************************************************************/

struct client final {
    client(const client &) = delete;
    client& operator= (const client &) = delete;

    client(ba::io_context &ioctx, const std::string &ip, std::uint16_t port, const std::string &fname, std::size_t ping)
        :m_socket{ioctx}
        ,m_queue{}
        ,m_on_write{false}
        ,m_ip{ip}
        ,m_port{port}
        ,m_state_fname{fname}
        ,m_ping{ping}
        ,m_ping_timer{ioctx}
        ,m_timeout_timer{ioctx}
    {}

    ~client() {
        stop();
    }

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
    void stop() {
        m_socket.shutdown(tcp::socket::shutdown_send);
        m_socket.close();
        m_ping_timer.cancel();
        m_timeout_timer.cancel();
        m_on_write = false;
    }

    void send(shared_buffer str) {
        //std::cout << "send: " << str << ", addr=" << (const void *)str.data() << std::endl;
        ba::post(
             m_socket.get_executor()
            ,[this, str=std::move(str)]
             () mutable
             { push_to_queue(std::move(str)); }
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

        start_read(make_buffer());
    }
    void start_read(shared_buffer buf) {
        auto *ptr = buf.get();
        ba::async_read_until(
             m_socket
            ,ba::dynamic_buffer(*ptr)
            ,'\n'
            ,[this, buf=std::move(buf)]
             (const bs::error_code &ec, std::size_t rd) mutable
             { on_readed(std::move(buf), ec, rd); }
        );
    }
    void on_readed(shared_buffer buf, const bs::error_code &ec, std::size_t rd) {
        if ( ec ) {
            std::cerr << "read error: " << ec.message() << std::endl;

            return;
        }

        auto str = make_buffer(buf->data(), buf->data() + rd);
        buf->erase(0, rd);

        std::cout << "readed: " << *str;

        start_read(std::move(buf));
    }

    void push_to_queue(shared_buffer str) {
        m_queue.push(std::move(str));

        if ( !m_on_write ) {
            m_on_write = true;
            str = std::move(m_queue.front());
            m_queue.pop();

            send_impl(std::move(str));
        }
    }
    void send_impl(shared_buffer str) {
        //std::cout << "send_impl: " << str << ", addr=" << (const void *)str.data() << std::endl;
        auto *ptr = str.get();
        ba::async_write(
             m_socket
            ,ba::dynamic_buffer(*ptr)
            ,[this, str=std::move(str)]
             (const bs::error_code &ec, std::size_t wr) mutable
             { on_sent(std::move(str), ec, wr); }
        );
    }
    void on_sent(shared_buffer str, const bs::error_code &ec, std::size_t) {
        //std::cout << "on_sent: " << str << ", addr=" << (const void *)str.data() << std::endl;
        if ( ec ) {
            std::cerr  << "send error: " << ec.message() << std::endl;
        }

        if ( !m_queue.empty() ) {
            str = std::move(m_queue.front());
            m_queue.pop();

            send_impl(std::move(str));
        } else {
            m_on_write = false;
        }
    }

private:
    tcp::socket m_socket;
    std::queue<shared_buffer> m_queue;
    bool m_on_write;
    std::string m_ip;
    std::uint16_t m_port;
    std::string m_state_fname;
    std::size_t m_ping;
    ba::steady_timer m_ping_timer;
    ba::steady_timer m_timeout_timer;
};

/**********************************************************************************************************************/

struct term_reader {
    term_reader(const term_reader &) = delete;
    term_reader& operator= (const term_reader &) = delete;

    term_reader(ba::io_context &ioctx)
        :m_ioctx{ioctx}
        ,m_stdin{m_ioctx, ::dup(STDIN_FILENO)}
    {}

    // CB's signature: void(error_code, shared_buffer)
    template<typename CB>
    void start(CB cb) {
        assert(m_stdin.is_open());

        ba::post(
             m_stdin.get_executor()
            ,[this, cb=std::move(cb)]
             () mutable
            { read(make_buffer(), std::move(cb)); }
        );
    }

private:
    template<typename CB>
    void read(shared_buffer buf, CB cb) {
        auto *str = buf.get();
        ba::async_read_until(
             m_stdin
            ,ba::dynamic_buffer(*str)
            ,'\n'
            ,[this, buf=std::move(buf), cb=std::move(cb)]
             (const bs::error_code &ec, std::size_t rd) mutable
             { readed(ec, rd, std::move(buf), std::move(cb)); }
        );
    }
    template<typename CB>
    void readed(const bs::error_code &ec, std::size_t rd, shared_buffer buf, CB cb) {
        if ( ec ) {
            cb(ec, shared_buffer{});
        } else {
            auto str = make_buffer(buf->data(), buf->data() + rd);
            buf->erase(0, rd);

            if ( *str == "exit\n" ) {
                m_ioctx.stop();

                return;
            }

            cb(ec, std::move(str));

            read(std::move(buf), std::move(cb));
        }
    }

private:
    ba::io_context &m_ioctx;
    ba::posix::stream_descriptor m_stdin;
};

/**********************************************************************************************************************/

struct: cmdargs::kwords_group {
    CMDARGS_OPTION_ADD(ip, std::string, "server IP", and_(port));
    CMDARGS_OPTION_ADD(port, std::uint16_t, "server PORT", and_(ip));
    CMDARGS_OPTION_ADD(fname, std::string, "the state file name (not used if not specified)", optional, default_<std::string>("tablestate.txt"));
    CMDARGS_OPTION_ADD(ping, std::size_t, "ping interval in milliseconds (not used if not specified)", optional, default_<std::size_t>(1000));

    CMDARGS_OPTION_ADD_HELP();
} const kwords;

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

    const auto ip    = args.get(kwords.ip);
    const auto port  = args.get(kwords.port);
    const auto fname = args.get(kwords.fname);
    const auto ping  = args.get(kwords.ping);

    // io_context + client
    ba::io_context ioctx;
    client cli{ioctx, ip, port, fname, ping};
    cli.start(
        [](const bs::error_code &ec) {
            if ( !ec ) {
                std::cout << "successfully connected!" << std::endl;
            } else {
                std::cout << "connection error: " << ec.message() << std::endl;
            }
        }
    );

    // for reading `stdin` asynchronously
    term_reader term{ioctx};
    term.start(
        [&cli](const bs::error_code &ec, shared_buffer str){
            if ( ec ) {
                std::cout << "term: read error: " << ec << std::endl;
            } else {
                //std::cout << "term: str=" << *str;;
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

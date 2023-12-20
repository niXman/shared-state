
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
#include "../common/fnv1a.hpp"
#include "../common/average.hpp"
#include "../common/string_buffer.hpp"

#include <boost/asio.hpp>

namespace ba = boost::asio;
namespace bs = boost::system;
using tcp = boost::asio::ip::tcp;

#include <iostream>
#include <queue>

/**********************************************************************************************************************/

struct client final {
    client(const client &) = delete;
    client& operator= (const client &) = delete;

    client(
         ba::io_context &ioctx
        ,const std::string &ip
        ,std::uint16_t port
        ,buffers_pool &str_pool
        ,const std::string &fname
        ,std::size_t ping_ms
    )
        :m_socket{ioctx}
        ,m_queue{}
        ,m_on_write{false}
        ,m_ip{ip}
        ,m_port{port}
        ,m_str_pool{str_pool}
        ,m_state_fname{fname}
        ,m_ping_ms{ping_ms}
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
            return start_impl([](error_info){});
        } else {
            return start_impl(std::move(cb)...);
        }
    }
    void stop() {
        m_socket.shutdown(tcp::socket::shutdown_send);
        m_socket.close();
        m_on_write = false;
    }
    void stop_ping() {
        m_ping_timer.cancel();
        m_timeout_timer.cancel();
    }

    void send(shared_buffer str) {
        //std::cout << "send: " << str << ", addr=" << (const void *)str.data() << std::endl;
        ba::dispatch(
             m_socket.get_executor()
            ,[this, str=std::move(str)]
             () mutable
             { push_to_queue(std::move(str)); }
        );
    }

    std::size_t avg_latency() const { return m_avg.avg(); }

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

        start_ping();
        restart_timeout_timer();

        start_read(make_buffer(m_str_pool));
    }
    void start_read(shared_buffer buf) {
        auto *ptr = buf.get();
        ba::async_read_until(
             m_socket
            ,ba::dynamic_buffer(ptr->string())
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

        auto str = make_buffer(m_str_pool, buf->data(), buf->data() + rd);
        buf->erase(0, rd);

        //std::cout << "readed: " << str->string();
        constexpr auto ping_cmd = fnv1a("PING");
        constexpr auto data_cmd = fnv1a("DATA");
        constexpr auto stop_cmd = fnv1a("STOP");
        const auto cmd = std::string_view{str->data(), 4};
        switch ( auto hash = fnv1a(cmd); hash ) {
            case ping_cmd: { handle_ping(std::move(str)); break; }
            case data_cmd: { handle_data(std::move(str)); break; }
            case stop_cmd: { handle_stop(std::move(str)); break; }
            default: {
                std::cerr << "wrong command received: " << cmd << std::endl;

                return;
            }
        }

        start_read(std::move(buf));
    }

    void handle_ping(shared_buffer) {
        restart_timeout_timer();
    }
    void handle_data(shared_buffer val) {
        std::cout << "handle_data: " << val->string() << std::flush;
    }
    void handle_stop(shared_buffer) {
        std::cout << "handle_stop: STOP received!" << std::endl;
        stop();
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
            ,ba::dynamic_buffer(ptr->string())
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

    void start_ping() {
        m_ping_timer.expires_after(std::chrono::milliseconds{m_ping_ms});
        m_ping_timer.async_wait([this](bs::error_code ec){ send_ping(ec); });
    }
    void send_ping(bs::error_code ec) {
        //std::cout << "ping!" << std::endl;
        if ( ec ) {
            std::cerr << "send_ping error: " << ec.message() << std::endl;

            return;
        }

        auto time = ms_time();
        auto timestr = std::to_string(time);

        static const char *ping_str = "PING ";
        auto str = make_buffer(m_str_pool, ping_str, ping_str + 5);
        str->append(timestr);
        str->append('\n');

        send(str);

        start_ping();
    }
    void restart_timeout_timer() {
        m_timeout_timer.expires_after(std::chrono::milliseconds{m_ping_ms * 2});
        m_timeout_timer.async_wait([this](bs::error_code ec){ on_timeout_timer_handler(ec); });
    }
    void on_timeout_timer_handler(bs::error_code ec) {
        if ( ec == ba::error::operation_aborted ) { return; }
        std::cout << "on_timeout_timer_handler: " << ec.message() << std::endl;
    }

private:
    tcp::socket m_socket;
    std::queue<shared_buffer> m_queue;
    bool m_on_write;
    std::string m_ip;
    std::uint16_t m_port;
    buffers_pool &m_str_pool;
    std::string m_state_fname;
    std::size_t m_ping_ms;
    ba::steady_timer m_ping_timer;
    ba::steady_timer m_timeout_timer;
    average<10> m_avg;
};

/**********************************************************************************************************************/

struct term_reader {
    term_reader(const term_reader &) = delete;
    term_reader& operator= (const term_reader &) = delete;

    term_reader(ba::io_context &ioctx, buffers_pool &str_pool)
        :m_ioctx{ioctx}
        ,m_str_pool{str_pool}
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
             { read(make_buffer(m_str_pool), std::move(cb)); }
        );
    }

private:
    template<typename CB>
    void read(shared_buffer buf, CB cb) {
        auto *str = buf.get();
        ba::async_read_until(
             m_stdin
            ,ba::dynamic_buffer(str->string())
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
            auto str = make_buffer(m_str_pool, buf->data(), buf->data() + rd);
            buf->erase(0, rd);

            cb(ec, std::move(str));

            read(std::move(buf), std::move(cb));
        }
    }

private:
    ba::io_context &m_ioctx;
    buffers_pool &m_str_pool;
    ba::posix::stream_descriptor m_stdin;
};

/**********************************************************************************************************************/

struct: cmdargs::kwords_group {
    CMDARGS_OPTION_ADD(ip, std::string, "server IP", and_(port));
    CMDARGS_OPTION_ADD(port, std::uint16_t, "server PORT", and_(ip));
    CMDARGS_OPTION_ADD(fname, std::string, "the state file name (not used if not specified)"
        ,optional, default_<std::string>("tablestate.txt"));
    CMDARGS_OPTION_ADD(ping, std::size_t, "ping interval in MS"
        ,optional, default_<std::size_t>(500));

    CMDARGS_OPTION_ADD_HELP();
    CMDARGS_OPTION_ADD_VERSION("0.0.1");
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
    if ( cmdargs::is_help_or_version_requested(std::cout, argv[0], args) ) {
        return EXIT_SUCCESS;
    }
    const auto ip    = args[kwords.ip];
    const auto port  = args[kwords.port];
    const auto fname = args[kwords.fname];
    const auto ping  = args[kwords.ping];

    // io_context + client
    ba::io_context ioctx;
    buffers_pool str_pool{1024};
    client cli{ioctx, ip, port, str_pool, fname, ping};
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
    term_reader term{ioctx, str_pool};
    term.start(
        [&ioctx, &cli](const bs::error_code &ec, shared_buffer str){
            if ( ec ) {
                std::cout << "term: read error: " << ec.message() << std::endl;
                cli.stop_ping();
            } else {
                if ( str->string() == "q\n" || str->string() == "exit\n" ) {
                    cli.stop();

                    return;
                }
                //std::cout << "term: str=" << *str;
                str->preppend("DATA ");
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

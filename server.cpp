
// ----------------------------------------------------------------------------
//                              Apache License
//                        Version 2.0, January 2004
//                     http://www.apache.org/licenses/
//
// This file is part of Shared-State(https://github.com/niXman/shared-state) project.
//
// This was a test task for implementing Shared-State server using asio.
//
// Copyright (c) 2023 niXman (github dot nixman dog pm.me). All rights reserved.
// ----------------------------------------------------------------------------


#include <boost/asio.hpp>
#include <boost/crc.hpp>

namespace ba = boost::asio;
namespace bs = boost::system;
using tcp = boost::asio::ip::tcp;

#include <iostream>
#include <thread>
#include <vector>
#include <set>
#include <memory>
#include <charconv>

/**********************************************************************************************************************/

std::uint32_t calcHeavyHash(const std::string_view str)
{
    // CRC32 is not heavy, but let's assume we're doing something really CPU-intensive here...
    boost::crc_32_type crc32;
    crc32.process_bytes(str.data(), str.size());
    return crc32.checksum();
}

struct hasher {
    hasher(ba::io_context &ioc)
        :m_strand{ioc}
    {}

    // F's signature: void(std::string hash)
    template<typename F>
    void hash(const std::string_view str, F f) {
        auto hash_int = calcHeavyHash(str);
        char hash_buf[16]{};
        auto [end_ptr, ec] = std::to_chars(std::begin(hash_buf), std::end(hash_buf), hash_int, 16);
        if ( ec == std::errc{} ) {
            f(std::string{std::begin(hash_buf), end_ptr});
        }
    }

private:
    ba::io_context::strand m_strand;
};

/**********************************************************************************************************************/

struct shared_state {
    shared_state(ba::io_context &ioc)
        :m_strand{ioc}
        ,m_hasher{ioc}
    {}

    // F's signature: void(bool updated, std::string_view key, std::string_view hash)
    template<typename F>
    void update(std::shared_ptr<std::string> msg, F f) {
        auto pos = msg->find(' ');
        if ( pos == std::string::npos ) {
            std::cerr << "wrong string received!" << std::endl;

            f(false, std::string_view{}, std::string_view{});

            return;
        }

        // since we performed the search here, we create the key and
        // value here so as not to search again.
        std::string_view key{msg->data(), pos};
        std::string_view val{msg->data() + pos + 1, msg->size() - pos - 1};

        ba::post(
             m_strand
            ,[this, msg=std::move(msg), key, val, f=std::move(f)]()
             { calculate_hash(std::move(msg), key, val, std::move(f)); }
        );
    }

private:
    template<typename F>
    void calculate_hash(std::shared_ptr<std::string> msg, std::string_view key, std::string_view val, F f) {
        m_hasher.hash(
            val
            ,[this, msg=std::move(msg), key, f=std::move(f)]
            (std::string hash) {
                // back to the our strand
                ba::post(
                     m_strand
                    ,[this, msg=std::move(msg), hash=std::move(hash), key, f=std::move(f)]
                     ()
                     { hash_calculated(key, std::move(hash), std::move(f)); }
                );
            }
        );
    }
    template<typename F>
    void hash_calculated(std::string_view key, std::string hash, F f) {
        // check for key
        auto it = m_map.find(key);
        if ( it == m_map.end() ) {
            auto inserted = m_map.emplace(key, std::move(hash));
            f(true, key, inserted.first->second);

            return;
        }

        // check for val
        if ( it->second != hash ) {
            f(true, key, it->second);

            return;
        }

        f(false, std::string_view{}, std::string_view{});
    }

private:
    ba::io_context::strand m_strand;
    std::map<std::string, std::string, std::less<>> m_map;
    hasher m_hasher;
};

/**********************************************************************************************************************/

struct session: std::enable_shared_from_this<session> {
    using holder_ptr = std::shared_ptr<session>;

    session(tcp::socket sock, shared_state &state)
        :m_sock{std::move(sock)}
        ,m_buf{}
        ,m_ready_write{true}
        ,m_state{state}
    {}

    void start(holder_ptr holder) {
        ba::async_read_until(
             m_sock
            ,m_buf
            ,'\n'
            ,[this, holder=std::move(holder)]
             (const bs::error_code& ec, std::size_t size) {
                readed(ec, size, std::move(holder));
            }
        );
    }
    void readed(const bs::error_code& ec, std::size_t, holder_ptr holder) {
        if ( ec ) {
            std::cerr << "read error: " << ec << std::endl;

            return;
        }

        std::istream is(&m_buf);
        auto str = std::make_shared<std::string>();
        std::getline(is, *str);

        m_state.update(
             str
            ,[this, holder=std::move(holder)]
             (bool really, std::string_view key, std::string_view hash)
            { updated(really, key, hash, std::move(holder)); }
        );
    }

    void send(std::shared_ptr<std::string> msg) {
        auto holder = shared_from_this();
        ba::post(
             m_sock.get_executor()
            ,[this, msg=std::move(msg), holder=std::move(holder)]
             ()
            { send_impl(std::move(msg), std::move(holder)); }
        );
    }

private:
    void updated(bool really, std::string_view key, std::string_view hash, holder_ptr holder) {
        std::cout << really << ":" << key << ":" << hash << std::endl;

        // back to the our strand
        ba::post(
             m_sock.get_executor()
            ,[this, holder=std::move(holder)]
             ()
            { start(std::move(holder)); }
        );
    }

private:
    void send_impl(std::shared_ptr<std::string> msg, holder_ptr holder) {
        if ( !m_ready_write ) {
            return;
        }

        m_ready_write = false;

        ba::async_write(
             m_sock
            ,ba::buffer(*msg)
            ,[this, msg=std::move(msg), holder=std::move(holder)]
             (const bs::error_code &ec, std::size_t)
             { sent(ec); }
        );
    }
    void sent(const bs::error_code &ec) {
        if ( ec ) {
            m_sock.close();

            return;
        }

        m_ready_write = true;
    }

private:
    tcp::socket m_sock;
    ba::streambuf m_buf;
    bool m_ready_write;
    shared_state &m_state;
};

/**********************************************************************************************************************/

struct session_manager {
    session_manager(ba::io_context &ioc)
        :m_strand{ioc}
        ,m_set{}
    {}

    void add(std::weak_ptr<session> sptr) {
        ba::post(
             m_strand
            ,[this, sptr](){
                m_set.insert(sptr);
            }
        );
    }

    void broadcast(std::shared_ptr<std::string> msg) {
        ba::post(
             m_strand
            ,[this, msg](){
                for ( auto it = m_set.begin(); it != m_set.end(); ) {
                    if ( auto sptr = it->lock(); sptr ) {
                        ++it;
                        sptr->send(msg);
                    } else {
                        it = m_set.erase(it);
                    }
                }
            }
        );
    }

private:
    ba::io_context::strand m_strand;
    std::set<
         std::weak_ptr<session>
        ,std::owner_less<std::weak_ptr<session>>
    > m_set;
};

/**********************************************************************************************************************/

struct acceptor {
    acceptor(ba::io_context &ioc, std::uint16_t port, session_manager &smgr, shared_state &state)
        :m_ioc{ioc}
        ,m_acc{ba::make_strand(m_ioc), tcp::endpoint{tcp::v4(), port}}
        ,m_smgr{smgr}
        ,m_state{state}
    {
        m_acc.set_option(ba::socket_base::reuse_address{true}); // can throw
        m_acc.set_option(tcp::no_delay{true}); // can throw
    }

    void start() {
        m_acc.async_accept(
             ba::make_strand(m_ioc)
            ,[this](const bs::error_code &ec, tcp::socket sock)
             { on_accepted(ec, std::move(sock)); }
        );
    }

private:
    void on_accepted(const bs::error_code &ec, tcp::socket sock) {
        if ( ec ) {
            std::cerr << "acceptor was stopped because of error: " << ec << std::endl;

            return;
        }

        auto sptr = std::make_shared<session>(std::move(sock), m_state);
        sptr->start(sptr->shared_from_this());

        m_smgr.add(sptr);

        start();
    }

private:
    ba::io_context &m_ioc;
    tcp::acceptor m_acc;
    session_manager &m_smgr;
    shared_state &m_state;
};

/**********************************************************************************************************************/

int main(int argc, char **argv) try {
    if ( argc != 3 ) {
        std::cout << "usage: " << argv[0] << " <PORT> <THREADS>(min 2)" << std::endl;

        return EXIT_FAILURE;
    }

    std::uint16_t port = std::atoi(argv[1]);
    int threads = std::atoi(argv[2]);
    threads -= 1;

    ba::io_context ioctx{threads};

    session_manager smgr{ioctx};
    shared_state state{ioctx};
    acceptor acc{ioctx, port, smgr, state};
    acc.start();

    ba::signal_set signals{ioctx, SIGINT, SIGTERM};
    signals.async_wait([&ioctx](const bs::error_code &, int) {
        std::cout << "SIGINT/SIGTERM received!" << std::endl;

        ioctx.stop();
    });

    std::vector<std::thread> threadsv;
    threadsv.reserve(threads);
    for ( ; threads; --threads ) {
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

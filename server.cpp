
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
#include <map>
#include <list>
#include <memory>
#include <charconv>

/**********************************************************************************************************************/

std::uint32_t calcHeavyHash(const std::string &str)
{
    // CRC32 is not heavy, but let's assume we're doing something really CPU-intensive here...
    boost::crc_32_type crc32;
    crc32.process_bytes(str.data(), str.size());
    return crc32.checksum();
}

struct hasher {
    struct queue_item {
        std::string str;
        std::string hash;
        std::function<void(std::string)> cb;
    };
    // `list` is used here because I need an iterator to an inserted element
    using queue_type = std::list<queue_item>;
    using queue_iterator = queue_type::iterator;

    hasher(ba::io_context &ioc)
        :m_ioc{ioc}
        ,m_strand{ioc}
    {}

    // CB's signature: void(std::string hash)
    template<typename CB>
    void hash(std::string str, CB cb) {
        ba::post(
             m_strand
            ,[this, str=std::move(str), cb=std::move(cb)]
             () mutable
            { hash_impl(std::move(str), std::move(cb)); }
        );
    }

private:
    template<typename CB>
    void hash_impl(std::string str, CB cb) {
        auto it = m_queue.insert(
             m_queue.begin()
            ,{std::move(str), std::string{}, std::move(cb)}
        );

        // post to io_context execution pool
        ba::post(
             m_ioc
            ,[this, it]() {
                auto hash_int = calcHeavyHash(it->str);
                char hash_buf[16]{};
                auto [end_ptr, ec] = std::to_chars(
                     hash_buf
                    ,std::end(hash_buf)
                    ,hash_int
                    ,16
                );
                std::string hash{"0x"};
                hash.append(hash_buf, end_ptr);
                it->hash = std::move(hash);

                // when finished - post back to our strand
                ba::post(
                     m_strand
                    ,[this, it]()
                    { on_hashed(it); }
                );
            }
        );
    }
    // called from our strand
    void on_hashed(queue_iterator it) {
        if ( it == m_queue.begin() ) {
            while ( it != m_queue.end() && !it->hash.empty() ) {
                it->cb(std::move(it->hash));
                it = m_queue.erase(it);
            }
        }
    }

private:
    ba::io_context &m_ioc;
    ba::io_context::strand m_strand;
    queue_type m_queue;
};

/**********************************************************************************************************************/

struct shared_state {
    shared_state(ba::io_context &ioc)
        :m_strand{ioc}
        ,m_hasher{ioc}
    {}

    // CB's signature: void(bool updated, const std::string &key, const std::string &hash)
    template<typename CB>
    void update(std::string key, std::string val, CB cb) {
        ba::post(
             m_strand
            ,[this, key=std::move(key), val=std::move(val), cb=std::move(cb)]
             () mutable
             { calculate_hash(std::move(key), std::move(val), std::move(cb)); }
        );
    }

private:
    template<typename CB>
    void calculate_hash(std::string key, std::string val, CB cb) {
        std::cout << "calculate_hash: key=" << key << ", val=" << val << std::endl;

        m_hasher.hash(
             std::move(val)
            ,[this, key=std::move(key), cb=std::move(cb)]
             (std::string hash) mutable {
                // back to the our strand
                ba::post(
                     m_strand
                    ,[this, hash=std::move(hash), key=std::move(key), cb=std::move(cb)]
                     () mutable
                     { hash_calculated(std::move(key), std::move(hash), std::move(cb)); }
                );
            }
        );
    }
    template<typename CB>
    void hash_calculated(std::string key, std::string hash, CB cb) {
        std::cout << "hash_calculated: key=" << key << ", hash=" << hash << std::endl;

        // check for key
        auto it = m_map.find(key);
        if ( it == m_map.end() ) {
            auto inserted = m_map.emplace(std::move(key), std::move(hash));
            cb(true, inserted.first->first, inserted.first->second);

            return;
        }

        // check for val
        if ( it->second != hash ) {
            it->second = std::move(hash);

            cb(true, it->first, it->second);

            return;
        }

        // just to avoid constructing the empty strings many times
        static const std::string empty_string;
        cb(false, empty_string, empty_string);
    }

private:
    ba::io_context::strand m_strand;
    std::map<std::string, std::string> m_map;
    hasher m_hasher;
};

/**********************************************************************************************************************/

struct session: std::enable_shared_from_this<session> {
    using holder_ptr = std::shared_ptr<session>;
    using broadcast_cb_type = std::function<void(std::shared_ptr<std::string> msg)>;

    session(tcp::socket sock, shared_state &state, broadcast_cb_type broadcast)
        :m_sock{std::move(sock)}
        ,m_buf{}
        ,m_ready_write{true}
        ,m_state{state}
        ,m_broadcast{std::move(broadcast)}
    {
        m_peer = m_sock.remote_endpoint();
    }

    void start(holder_ptr holder) {
        ba::async_read_until(
             m_sock
            ,m_buf
            ,'\n'
            ,[this, holder=std::move(holder)]
             (const bs::error_code& ec, std::size_t size) mutable
             {readed(ec, size, std::move(holder)); }
        );
    }
    void readed(const bs::error_code& ec, std::size_t, holder_ptr holder) {
        if ( ec ) {
            if ( ec == ba::error::eof ) {
                std::cerr
                    << "the client("
                    << m_peer.address().to_string() << ":" << m_peer.port()
                    << ") disconnected"
                    << std::endl;
            } else {
                std::cerr << "read error: " << ec << std::endl;
            }

            return;
        }

        std::istream is(&m_buf);
        std::string str;
        std::getline(is, str);

        auto pos = str.find(' ');
        if ( pos == std::string::npos ) {
            std::cerr << "wrong string received: \"" << str << "\"" << std::endl;

            start(std::move(holder));

            return;
        }

        std::string key{str.data(), pos};
        std::string val{str.data() + pos + 1, str.size() - pos - 1};

        m_state.update(
             std::move(key)
            ,std::move(val)
            ,[this, holder=std::move(holder)]
             (bool really, const std::string &key, const std::string &hash) mutable
             { updated(really, key, hash, std::move(holder)); }
        );
    }

    void send(std::shared_ptr<std::string> msg) {
        auto holder = shared_from_this();
        ba::post(
             m_sock.get_executor()
            ,[this, msg=std::move(msg), holder=std::move(holder)]
             () mutable
             { send_impl(std::move(msg), std::move(holder)); }
        );
    }

private:
    // still called from state's strand
    void updated(bool really, const std::string &key, const std::string &hash, holder_ptr holder) {
        std::cout << really << ":" << key << ":" << hash << std::endl;

        if ( really ) {
            auto msg = std::make_shared<std::string>();
            *msg += key;
            *msg += ' ';
            *msg += hash;
            *msg += '\n';

            m_broadcast(std::move(msg));
        }

        // back to the our strand
        ba::post(
             m_sock.get_executor()
            ,[this, holder=std::move(holder)]
             () mutable
             { start(std::move(holder)); }
        );
    }

private:
    void send_impl(std::shared_ptr<std::string> msg, holder_ptr holder) {
        if ( !m_ready_write ) {
            return;
        }

        m_ready_write = false;

        const auto *ptr = msg->data();
        const auto size = msg->size();
        ba::async_write(
             m_sock
            ,ba::buffer(ptr, size)
            ,[this, msg=std::move(msg), holder=std::move(holder)]
             (const bs::error_code &ec, std::size_t)
             { sent(ec); }
        );
    }
    void sent(const bs::error_code &ec) {
        if ( ec ) {
            std::cerr << "write error: " << ec << std::endl;

            m_sock.close();

            return;
        }

        m_ready_write = true;
    }

private:
    tcp::socket m_sock;
    tcp::endpoint m_peer;
    ba::streambuf m_buf;
    bool m_ready_write;
    shared_state &m_state;
    broadcast_cb_type m_broadcast;
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
            ,[this, msg=std::move(msg)](){
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
            std::cerr << "acceptor error: " << ec << std::endl;

            return;
        }

        const auto rep = sock.remote_endpoint();
        std::cout << "new connection from: " << rep.address().to_string() << ":" << rep.port() << std::endl;

        auto sptr = std::make_shared<session>(
             std::move(sock)
            ,m_state
            ,[this]
             (std::shared_ptr<std::string> msg)
             { m_smgr.broadcast(std::move(msg)); }
        );

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

    shared_state state{ioctx};
    session_manager smgr{ioctx};
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

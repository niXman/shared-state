
#ifndef __shared_state_server__acceptor_hpp__included
#define __shared_state_server__acceptor_hpp__included

#include "utils.hpp"

#include <boost/asio/io_context.hpp>

#include <list>

/**********************************************************************************************************************/

struct acceptor: noncopyable_nonmovable {
    acceptor(ba::io_context &ioc, const std::string &ip, std::uint16_t port)
        :m_ioc{ioc}
        ,m_acc{ba::make_strand(m_ioc), tcp::endpoint{ba::ip::make_address(ip), port}}
    {
        m_acc.set_option(ba::socket_base::reuse_address{true}); // can throw
        m_acc.set_option(tcp::no_delay{true}); // can throw
    }

    // OnAcceptedCB's signature: void(tcp::socket)
    // ErrorCB's signature: void(const char *file, int line, const char *function, error_category cat, bs::error_code)
    template<typename OnAcceptedCB, typename ErrorCB>
    void start(OnAcceptedCB on_accepted_cb, ErrorCB error_cb) {
        m_acc.async_accept(
             ba::make_strand(m_ioc)
            ,[this, on_accepted_cb=std::move(on_accepted_cb), error_cb=std::move(error_cb)]
             (const bs::error_code &ec, tcp::socket sock) mutable
             { on_accepted(std::move(on_accepted_cb), std::move(error_cb), ec, std::move(sock)); }
        );
    }

private:
    template<typename OnAcceptedCB, typename ErrorCB>
    void on_accepted(OnAcceptedCB on_accepted_cb, ErrorCB error_cb, const bs::error_code &ec, tcp::socket sock) {
        if ( ec ) {
            CALL_ERROR_HANDLER(error_cb, ec);

            return;
        }

        on_accepted_cb(std::move(sock));

        // m_state.get_size(
        //     [this, on_connected_cb, sock=std::move(sock)]
        //     (std::size_t size) mutable {
        //         ba::post(
        //             sock.get_executor()
        //             ,[this, size, on_connected_cb=std::move(on_connected_cb), sock=std::move(sock)]
        //              () mutable
        //              { on_size_received(std::move(on_connected_cb), size, std::move(sock)); }
        //         );
        //     }
        // );

        start(std::move(on_accepted_cb), std::move(error_cb));
    }
    // template<typename OnConnectedCB>
    // void on_size_received(OnConnectedCB on_connected_cb, std::size_t size, tcp::socket sock) {
    //     on_connected_cb(sock.remote_endpoint(), size);

    //     auto sptr = std::make_shared<session>(std::move(sock), m_state);
    //     m_smgr.add(sptr);

    //     sptr->start(
    //          [this]
    //          (shared_buffer msg)
    //          { m_smgr.broadcast(std::move(msg)); }
    //         ,sptr->shared_from_this()
    //     );
    // }

private:
    ba::io_context &m_ioc;
    tcp::acceptor m_acc;
};

/**********************************************************************************************************************/

#endif // __shared_state_server__acceptor_hpp__included

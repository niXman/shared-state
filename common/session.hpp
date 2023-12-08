
#ifndef __shared_state_server__session_hpp__included
#define __shared_state_server__session_hpp__included

#include "utils.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

/**********************************************************************************************************************/

struct session: std::enable_shared_from_this<session> {
    session(tcp::socket sock/*, shared_state &state*/)
        :m_sock{std::move(sock)}
        ,m_peer{m_sock.remote_endpoint()}
        //,m_state{state}
    {}

    // ReadedCB's signature: void(shared_buf, holder_ptr)
    // ErrorCB's signature: void(const char *file, int line, const char *function, error_category cat, bs::error_code ec)
    template<typename ReadedCB, typename ErrorCB>
    void start(ReadedCB readed_cb, ErrorCB error_cb, holder_ptr holder = {}) {
        start_read(std::move(readed_cb), error_cb, make_buffer(), holder);

        // start to sync the state
        // m_state.get_first(
        //     [this, holder=std::move(holder)]
        //     (bool empty, auto it, const shared_buffer &key, const shared_buffer &hash, const shared_buffer &key_hash) mutable
        //     { on_received_first(empty, it, key, hash, key_hash, std::move(holder)); }
        //     );
    }

    // may be called from any thread
    // OnSentCB's signature: void()
    // ErrorCB's signature: void(const char *file, int line, const char *function, error_category cat, bs::error_code ec)
    template<typename OnSentCB, typename ErrorCB>
    void send(OnSentCB on_sent_cb, ErrorCB error_cb, shared_buffer msg, holder_ptr holder) {
        ba::post(
             m_sock.get_executor()
            ,[this, on_sent_cb=std::move(on_sent_cb), error_cb=std::move(error_cb), msg=std::move(msg), holder=std::move(holder)]
             () mutable
             { send_impl(std::move(on_sent_cb), std::move(error_cb), std::move(msg), std::move(holder)); }
        );
    }

    auto& get_socket() { return m_sock; }
    auto endpoint() const { return m_sock.remote_endpoint(); }

private:
    // called from shared_state's strand
    // template<typename It>
    // void on_received_first(
    //     bool empty
    //     ,It it
    //     ,const shared_buffer &/*key*/
    //     ,const shared_buffer &/*hash*/
    //     ,const shared_buffer &key_hash
    //     ,holder_ptr holder)
    // {
    //     if ( !empty ) {
    //         ba::post(
    //             m_sock.get_executor()
    //             ,[this, it, key_hash, holder=std::move(holder)]
    //             () mutable
    //             {
    //                 send(
    //                     key_hash
    //                     ,[this, it, holder2=holder]
    //                     (const bs::error_code &ec)
    //                     { on_get_first_sent(ec, it, std::move(holder2)); }
    //                     ,std::move(holder)
    //                     );
    //             }
    //             );
    //     } else {
    //         //DEBUG_EXPR(std::cout << "the map is empty, nothing to update!" << std::endl;);
    //     }
    // }
    // template<typename It>
    // void on_get_first_sent(const bs::error_code &ec, It it, holder_ptr holder) {
    //     if ( ec ) {
    //         std::cerr << "on_get_first_sent error: " << ec << std::endl;

    //         return;
    //     }

    //     m_state.get_next(
    //         it
    //         ,[this, holder=std::move(holder)]
    //         (bool at_end, auto it, const shared_buffer &key, const shared_buffer &hash, const shared_buffer &key_hash)
    //         { on_received_first(at_end, it, key, hash, key_hash, std::move(holder)); }
    //         );
    // }

private:
    template<typename ReadedCB, typename ErrorCB>
    void start_read(ReadedCB readed_cb, ErrorCB error_cb, shared_buffer buf, holder_ptr holder) {
        auto *ptr = buf.get();
        ba::async_read_until(
             m_sock
            ,ba::dynamic_buffer(*ptr)
            ,'\n'
            ,[this, buf=std::move(buf), readed_cb=std::move(readed_cb), error_cb=std::move(error_cb), holder=std::move(holder)]
             (const bs::error_code& ec, std::size_t size) mutable
             { on_readed(std::move(readed_cb), std::move(error_cb), std::move(buf), ec, size, std::move(holder)); }
        );
    }
    template<typename ReadedCB, typename ErrorCB>
    void on_readed(ReadedCB readed_cb, ErrorCB error_cb, shared_buffer buf, const bs::error_code& ec, std::size_t rd, holder_ptr holder) {
        if ( ec ) {
            CALL_ERROR_HANDLER(error_cb, ec);

            return;
        }

        auto str = make_buffer(buf->data(), buf->data() + rd);
        buf->erase(0, rd);

        readed_cb(std::move(str), holder);

        start_read(std::move(readed_cb), std::move(error_cb), std::move(buf), std::move(holder));
    }

private:
    // called from shared_state's strand
    template<typename CB>
    void on_updated(
        bool really
        ,const shared_buffer &/*key*/
        ,const shared_buffer &/*hash*/
        ,const shared_buffer &key_hash
        ,CB broadcast_cb
        ,holder_ptr /*holder*/)
    {
        // when `really` == true, it's mean that `shared_state` was really updated
        if ( really ) {

            //DEBUG_EXPR(std::cout << "broadcasting: " << *msg << std::flush;);

            broadcast_cb(key_hash);
        }
    }

private:
    template<typename OnSentCB, typename ErrorCB>
    void send_impl(OnSentCB on_sent_cb, ErrorCB error_cb, shared_buffer msg, holder_ptr holder) {
        //std::cout << "send_impl(string): " << *msg << std::endl;

        auto *ptr = msg.get();
        ba::async_write(
             m_sock
            ,ba::buffer(*ptr)
            ,[this, on_sent_cb=std::move(on_sent_cb), error_cb=std::move(error_cb), msg=std::move(msg), holder=std::move(holder)]
             (const bs::error_code &ec, std::size_t) mutable
             { sent(std::move(on_sent_cb), std::move(error_cb), ec); }
        );
    }
    template<typename OnSentCB, typename ErrorCB>
    void sent(OnSentCB on_sent_cb, ErrorCB error_cb, const bs::error_code &ec) {
        if ( ec ) {
            CALL_ERROR_HANDLER(error_cb, ec);

            m_sock.close();

            return;
        }

        on_sent_cb();
    }

private:
    tcp::socket m_sock;
    tcp::endpoint m_peer;
//    shared_state &m_state;
};

/**********************************************************************************************************************/

#endif // __shared_state_server__session_hpp__included

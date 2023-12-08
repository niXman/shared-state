
#ifndef __shared_state_server__value_hasher_hpp__included
#define __shared_state_server__value_hasher_hpp__included

#include "utils.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/intrusive/list.hpp>

/**********************************************************************************************************************/

struct value_hasher: noncopyable_nonmovable {
    value_hasher(ba::io_context &ioctx, std::size_t recalculate)
        :m_ioctx{ioctx}
        ,m_strand{ioctx}
        ,m_queue{}
        ,m_recalculate{recalculate}
    {}

    // CB's signature: void(std::string hash)
    template<typename CB>
    void hash(shared_buffer str, CB cb) {
        ba::post(
             m_strand
            ,[this, str=std::move(str), cb=std::move(cb)]
             () mutable
             { put_to_queue(std::move(str), std::move(cb)); }
        );
    }

private:
    template<typename CB>
    void put_to_queue(shared_buffer str, CB cb) {
        auto *item = new queue_item{std::move(str), shared_buffer{}, std::move(cb)};
        auto it = m_queue.insert(
             m_queue.end()
            ,*item
        );

        post_for_execution(it);
    }
    template<typename Iter>
    void post_for_execution(Iter it) {
        // post to io_context execution pool
        ba::post(
             m_ioctx
            ,[this, it]() {
                boost::uuids::detail::sha1 sha1;
                char hash_buf[43] = {'0', 'x'};

                for ( auto idx = 0u; idx != m_recalculate; ++idx ) {
                    unsigned hash_dig[5] = {0};
                    sha1.process_bytes(it->str->data(), it->str->size());
                    sha1.get_digest(hash_dig);

                    char *hash_buf_it = hash_buf + 2;
                    auto *dig_it = std::begin(hash_dig);
                    for ( ; dig_it != std::end(hash_dig); ++dig_it, hash_buf_it += 8 ) {
                        std::sprintf(hash_buf_it, "%08x", *dig_it);
                    }
                }

                auto hash = make_buffer(hash_buf, hash_buf + 42);
                // when finished - post back to our strand
                ba::post(
                     m_strand
                    ,[this, it, hash=std::move(hash)]
                     () mutable
                     { on_executed(it, std::move(hash)); }
                );
            }
        );
    }

    // called from our strand
    template<typename Iter>
    void on_executed(Iter it, shared_buffer hash) {
        it->hash = std::move(hash);
        if ( it == m_queue.begin() ) {
            while ( it != m_queue.end() && it->hash && !it->hash->empty() ) {
                auto newit = it;
                newit->cb(std::move(it->hash));
                it = m_queue.erase(newit);
                delete newit;
            }
        }
    }

private:
    ba::io_context &m_ioctx;
    ba::io_context::strand m_strand;

    struct queue_item: boost::intrusive::list_base_hook<> {
        shared_buffer str;
        shared_buffer hash;
        std::function<void(shared_buffer)> cb;
    };
    using queue_type = boost::intrusive::list<queue_item>;
    using queue_iterator = queue_type::iterator;

    queue_type m_queue;
    std::size_t m_recalculate;
};

/**********************************************************************************************************************/

#endif // __shared_state_server__value_hasher_hpp__included

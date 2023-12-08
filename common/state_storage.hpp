
#ifndef __shared_state_server__state_storage_hpp__included
#define __shared_state_server__state_storage_hpp__included

#include "utils.hpp"
#include "value_hasher.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

/**********************************************************************************************************************/

struct state_storage: noncopyable_nonmovable {
    state_storage(ba::io_context &ioc, value_hasher &hasher)
        :m_strand{ioc}
        ,m_hasher{hasher}
    {}

    // CB's signature: void(bool updated, const shared_buffer &key, const shared_buffer &hash, const shared_buffer &key_hash)
    template<typename CB>
    void update(shared_buffer key, shared_buffer val, CB cb) {
        ba::post(
             m_strand
            ,[this, key=std::move(key), val=std::move(val), cb=std::move(cb)]
             () mutable
             { calculate_hash(std::move(key), std::move(val), std::move(cb)); }
        );
    }

    auto get_size() {
        return ba::post(m_strand, ba::use_future([this](){ return m_map.size(); }));
    }

    template<typename Iter>
    struct storage_item_type {
        bool latest;
        Iter iterator;
        shared_buffer key;
        shared_buffer hash;
        shared_buffer key_hash;
    };

private:
    template<typename Iter>
    auto make_storage_item(
         bool latest
        ,Iter it
        ,const shared_buffer &k
        ,const shared_buffer &h
        ,const shared_buffer &kh)
    {
        return storage_item_type<Iter>{latest, it, k, h, kh};
    }

    auto get_first_impl() {
        auto it = m_map.begin();
        if ( it != m_map.end() ) {
            return make_storage_item(false, it, it->first, it->second.hash, it->second.key_hash);
        }

        return make_storage_item(true, it, m_null_buffer, m_null_buffer, m_null_buffer);
    }
    template<typename Iter>
    auto get_next_impl(Iter it) {
        it = std::next(it);
        if ( it != m_map.end() ) {
            return make_storage_item(false, it, it->first, it->second.hash, it->second.key_hash);
        }

        return make_storage_item(true, it, m_null_buffer, m_null_buffer, m_null_buffer);
    }

public:
    auto get_first() {
        return ba::post(m_strand, ba::use_future([this](){ return get_first_impl(); }));
    }

    template<typename Iter>
    auto get_next(const storage_item_type<Iter> &prev) {
        Iter it = prev.iterator;
        return ba::post(m_strand, ba::use_future([this, it](){ return get_next_impl(it); }));
    }

private:
    template<typename CB>
    void calculate_hash(shared_buffer key, shared_buffer val, CB cb) {
        //DEBUG_EXPR(std::cout << "calculate_hash: key=" << *key << ", val=" << *val << std::endl;);

        m_hasher.hash(
            std::move(val)
            ,[this, key=std::move(key), cb=std::move(cb)]
            (shared_buffer hash) mutable {
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
    void hash_calculated(shared_buffer key, shared_buffer hash, CB cb) {
        //DEBUG_EXPR(std::cout << "hash_calculated: key=" << *key << ", hash=" << *hash << std::endl;);

        // check for key
        auto it = m_map.find(key);
        if ( it == m_map.end() ) {
            auto key_hash = make_buffer(*key);
            *key_hash += ' ';
            *key_hash += *hash;
            *key_hash += '\n';
            auto inserted = m_map.emplace(std::move(key), map_value{std::move(hash), std::move(key_hash)});
            cb(true, inserted.first->first, inserted.first->second.hash, inserted.first->second.key_hash);

            return;
        }

        // check for val
        if ( *(it->second.hash) != *hash ) {
            auto key_hash = make_buffer(*(it->first));
            *key_hash += ' ';
            *key_hash += *hash;
            *key_hash += '\n';

            it->second.hash = std::move(hash);
            it->second.key_hash = std::move(key_hash);

            cb(true, it->first, it->second.hash, it->second.key_hash);

            return;
        }

        // no changes case
        cb(false, shared_buffer{}, shared_buffer{}, shared_buffer{});
    }

private:
    ba::io_context::strand m_strand;

    struct my_cmp {
        bool operator()(const shared_buffer &l, const shared_buffer &r) const {
            return *l < *r;
        }
    };
    struct map_value {
        shared_buffer hash;
        shared_buffer key_hash; // just for optimisation
    };
    std::map<shared_buffer, map_value, my_cmp> m_map;
    value_hasher &m_hasher;

    static const shared_buffer m_null_buffer;
};

const shared_buffer state_storage::m_null_buffer{};

/**********************************************************************************************************************/

#endif // __shared_state_server__state_storage_hpp__included

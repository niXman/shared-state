
// ----------------------------------------------------------------------------
//                              Apache License
//                        Version 2.0, January 2004
//                     http://www.apache.org/licenses/
//
// This file is part of shared-state-server(https://github.com/niXman/shared-state-server) project.
//
// This was a test task for implementing multithreaded Shared-State server using asio.
//
// Copyright (c) 2023 niXman (github dot nixman dog pm.me). All rights reserved.
// ----------------------------------------------------------------------------

#ifndef __shared_state_server__state_storage_hpp__included
#define __shared_state_server__state_storage_hpp__included

#include "utils.hpp"
#include "string_buffer.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/intrusive/set.hpp>

/**********************************************************************************************************************/

struct state_storage {
    state_storage(const state_storage &) = delete;
    state_storage& operator= (const state_storage &) = delete;
    state_storage(state_storage &&) = delete;
    state_storage& operator= (state_storage &&) = delete;

    state_storage(ba::io_context &ioctx)
        :m_strand{ioctx}
        ,m_map{}
    {}

    // CB's signature: void(shared_buffer buf)
    // called only when the storage was really updated (a new key-val pair was added, or value for the concrete key was changed)
    template<typename CB>
    auto update(const std::string_view key, const std::string_view val, shared_buffer buf, CB cb) {
        return ba::post(
             m_strand
            ,[this, key, val, buf=std::move(buf), cb=std::move(cb)]
             () mutable
             { update_impl(key, val, std::move(buf), std::move(cb)); }
        );
    }

    auto reset() {
        return ba::post(
             m_strand
            ,ba::use_future([this](){ m_map.clear_and_dispose([](auto *p){ delete p; }); })
        );
    }

    auto size()
    { return ba::post(m_strand, ba::use_future([this](){ return m_map.size(); })); }

private:
    auto get_first_impl() {
        auto it = m_map.begin();
        if ( it != m_map.end() ) {
            auto buf = it->key_val;
            return std::make_tuple(false, it, std::move(buf));
        }

        return std::make_tuple(true, it, shared_buffer{});
    }
    template<typename Iter>
    auto get_next_impl(Iter it) {
        it = std::next(it);
        if ( it != m_map.end() ) {
            auto buf = it->key_val;
            return std::make_tuple(false, it, std::move(buf));
        }

        return std::make_tuple(true, it, shared_buffer{});
    }

public:
    auto get_first() {
        auto fut = ba::post(m_strand, ba::use_future([this](){ return get_first_impl(); }));
        return fut.get();
    }

    template<typename Iter>
    auto get_next(Iter &it) {
        auto fut = ba::post(m_strand, ba::use_future([this, it](){ return get_next_impl(it); }));
        return fut.get();
    }

private:
    template<typename CB>
    void update_impl(const std::string_view key, const std::string_view val, shared_buffer buf, CB cb) {
        //DEBUG_EXPR(std::cout << "hash_calculated: key=" << *key << ", hash=" << *hash << std::endl;);

        // check for key
        auto it = m_map.find(key);
        if ( it == m_map.end() ) {
            auto *value = ::new map_value{key, val, std::move(buf)};
            auto inserted = m_map.insert(*value);
            cb(inserted.first->key_val);

            return;
        }

        // check for val
        if ( it->val != val ) {
            it->key = key;
            it->val = val;
            it->key_val = std::move(buf);

            cb(it->key_val);

            return;
        }
    }

private:
    struct map_value: boost::intrusive::set_base_hook<> {
        map_value(const std::string_view k, const std::string_view v, shared_buffer kv)
            :key{k}
            ,val{v}
            ,key_val{std::move(kv)}
        {}

        std::string_view key;
        std::string_view val;
        shared_buffer key_val;
    };
    struct get_key {
        using type = std::string_view;
        const type& operator() (const map_value &v) const noexcept
        { return v.key; }
    };

    ba::io_context::strand m_strand;
    boost::intrusive::set<
         map_value
        ,boost::intrusive::key_of_value<get_key>
    > m_map;
};

/**********************************************************************************************************************/

#endif // __shared_state_server__state_storage_hpp__included

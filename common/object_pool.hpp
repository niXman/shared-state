
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

#ifndef __shared_state_server__object_pool_hpp__included
#define __shared_state_server__object_pool_hpp__included

#include <boost/lockfree/stack.hpp>

#include <atomic>

/**********************************************************************************************************************/

template<typename T>
struct object_pool final {
    object_pool(const object_pool &) = delete;
    object_pool& operator= (const object_pool &) = delete;
    object_pool(object_pool &&) = default;
    object_pool& operator= (object_pool &&) = default;

    object_pool(std::size_t capacity)
        :m_size{}
        ,m_in_use{}
        ,m_stack{capacity}
    {}
    ~object_pool()
    { assert(m_in_use == 0); }

    template<typename ...Args>
    auto get(Args && ...args) { return get_impl(std::forward<Args>(args)...); }

    template<typename Deleter, typename ...Args>
    auto get_del(Deleter del, Args && ...args)
    { return get_impl_del(std::move(del), std::forward<Args>(args)...); }

    std::size_t size() const noexcept { return m_size.load(); }
    std::size_t in_use() const noexcept { return m_in_use.load(); }

private:
    template<typename ...Args>
    auto get_impl(Args && ...args) {
        static const auto deleter = [this](T *p) {
            while ( !m_stack.push(p) )
            {}

            --m_in_use;
        };

        if ( !m_stack.empty() ) {
            T *p;
            while ( !m_stack.pop(p) )
            {}

            T *np = ::new(static_cast<void *>(p)) T(std::forward<Args>(args)...);
            ++m_in_use;

            return intrusive_ptr<T>{np, std::move(deleter)};
        }

        T *p = ::new T(std::forward<Args>(args)...);
        ++m_size;
        ++m_in_use;

        return intrusive_ptr<T>{p, std::move(deleter)};
    }

    template<typename Deleter, typename ...Args>
    auto get_impl_del(Deleter del, Args && ...args) {
        static const auto deleter = [this, del=std::move(del)](T *p) {
            del(p);

            while ( !m_stack.push(p) )
            {}

            --m_in_use;
        };

        if ( !m_stack.empty() ) {
            T *p;
            while ( !m_stack.pop(p) )
            {}

            T *np = ::new(static_cast<void *>(p)) T(std::forward<Args>(args)...);
            ++m_in_use;

            return intrusive_ptr<T>{np, std::move(deleter)};
        }

        T *p = ::new T(std::forward<Args>(args)...);
        ++m_size;
        ++m_in_use;

        return intrusive_ptr<T>{p, std::move(deleter)};
    }

private:
    std::atomic_uint32_t m_size;
    std::atomic_uint32_t m_in_use;
    boost::lockfree::stack<T *> m_stack;
};

/**********************************************************************************************************************/

#endif // __shared_state_server__object_pool_hpp__included

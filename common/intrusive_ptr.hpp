
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

#ifndef __shared_state_server__intrusive_ptr_hpp__included
#define __shared_state_server__intrusive_ptr_hpp__included

#include <utility>

#include "intrusive_base.hpp"

/**********************************************************************************************************************/

namespace details {

/**********************************************************************************************************************/

template<typename U>
struct dereference_type {
    using type = U &;
};

template<>
struct dereference_type<void> {
    using type = void;
};

/**********************************************************************************************************************/

} // ns details

/**********************************************************************************************************************/

template<typename T>
struct intrusive_ptr {
private:
    T *m_ptr;

public:
    using element_type = T;

    intrusive_ptr() noexcept
        :m_ptr{nullptr}
    {}
    intrusive_ptr(T *ptr) noexcept
        :m_ptr{ptr}
    {
        static_cast<intrusive_base<T> *>(m_ptr)
            ->intrusive_set_deleter(details::default_delete<T>{});
        static_cast<details::intrusive_base_base *>(m_ptr)->intrusive_increment_ref();
    }
    template<typename Deleter>
    intrusive_ptr(T *ptr, Deleter del) noexcept
        :m_ptr{ptr}
    {
        static_cast<intrusive_base<T> *>(m_ptr)
            ->intrusive_set_deleter(details::user_delete<T, Deleter>{std::move(del)});
        static_cast<details::intrusive_base_base *>(m_ptr)->intrusive_increment_ref();
    }

    intrusive_ptr(const intrusive_ptr &r) noexcept
        :m_ptr{r.m_ptr}
    {
        static_cast<details::intrusive_base_base *>(m_ptr)->intrusive_increment_ref();
    }
    intrusive_ptr& operator= (const intrusive_ptr &r) noexcept {
        m_ptr = r.m_ptr;
        static_cast<details::intrusive_base_base *>(m_ptr)->intrusive_increment_ref();

        return *this;
    }
    intrusive_ptr(intrusive_ptr &&r) noexcept
        :m_ptr{r.m_ptr}
    {
        r.m_ptr = nullptr;
    }
    intrusive_ptr& operator= (intrusive_ptr &&r) noexcept {
        m_ptr = r.m_ptr;
        r.m_ptr = nullptr;

        return *this;
    }

    ~intrusive_ptr() noexcept {
        auto *base = static_cast<details::intrusive_base_base *>(m_ptr);
        if ( base && 0 == base->intrusive_decrement_ref() ) {
            base->intrusive_call_deleter(m_ptr);
        }
    }

    explicit      operator bool() const noexcept { return m_ptr; }
    const T*      get()           const noexcept { return m_ptr; }
    T*            get()                 noexcept { return m_ptr; }
    const T*      operator-> ()   const noexcept { return m_ptr; }
    T*            operator-> ()         noexcept { return m_ptr; }
    using deref_t = typename details::dereference_type<T>::type;
    const deref_t operator*  ()   const noexcept { return *m_ptr; }
    deref_t       operator*  ()         noexcept { return *m_ptr; }
};

template<typename T, typename ...Args>
auto make_intrusive(Args && ...args) noexcept {
    return intrusive_ptr<T>{::new T(std::forward<Args>(args)...)};
}

template<typename T, typename Deleter, typename ...Args>
auto make_intrusive_del(Deleter &&del, Args && ...args) noexcept {
    return intrusive_ptr<T>{::new T(std::forward<Args>(args)...), std::forward<Deleter>(del)};
}

/**********************************************************************************************************************/

#endif // __shared_state_server__intrusive_ptr_hpp__included

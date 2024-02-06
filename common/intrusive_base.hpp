
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

#ifndef __shared_state_server__intrusive_base_hpp__included
#define __shared_state_server__intrusive_base_hpp__included

#include <atomic>
#include <utility>
#include <cstdint>

template<typename T>
struct intrusive_ptr;

/**********************************************************************************************************************/

namespace details {

template<typename T>
struct deleter_holder {
    virtual void delete_(T *) const = 0;
};

template<typename T>
struct default_delete: deleter_holder<T> {
    void delete_(T *p) const override { delete p; }
};

template<typename T, typename Deleter>
struct user_delete: deleter_holder<T> {
    user_delete(Deleter del)
        :m_del{std::move(del)}
    {}
    void delete_(T *p) const override { m_del(p); }

private:
    Deleter m_del;
};

struct intrusive_base_base {
    template<typename>
    friend struct intrusive_ptr;

    intrusive_base_base() noexcept
        :m_counter{1u}
    {}
    intrusive_base_base(const intrusive_base_base &) noexcept
        :m_counter{1u}
    {}

    void intrusive_increment_ref() noexcept
    { m_counter.fetch_add(1, std::memory_order_acq_rel); }

    std::uint32_t intrusive_decrement_ref() noexcept
    { return m_counter.fetch_sub(1, std::memory_order_acq_rel) - 1; }

    std::uint32_t intrusive_use_count() const noexcept
    { return m_counter.load(); }

    virtual void intrusive_call_deleter(void *) = 0;

protected:
    virtual ~intrusive_base_base() noexcept = default;

private:
    std::atomic_uint_least32_t m_counter;
};

} // ns details

/**********************************************************************************************************************/

template<typename Derived, std::size_t DeleterBytes = 32u>
struct intrusive_base: details::intrusive_base_base {
    template<typename>
    friend struct intrusive_ptr;

    intrusive_base() noexcept
        :intrusive_base_base{}
        ,m_arr{}
        ,m_del{nullptr}
    {}
    intrusive_base(const intrusive_base &) noexcept
        :intrusive_base_base{}
        ,m_arr{}
        ,m_del{nullptr}
    {}

protected:
    virtual ~intrusive_base() noexcept = default;

private:
    template<typename Deleter>
    void intrusive_set_deleter(Deleter del) noexcept {
        static_assert(sizeof(m_arr) >= sizeof(Deleter));
        m_del = ::new(std::addressof(m_arr)) Deleter{std::move(del)};
    }
    virtual void intrusive_call_deleter(void *p) override
    { m_del->delete_(static_cast<Derived *>(p)); }

private:
    typename std::aligned_storage<DeleterBytes>::type m_arr;
    details::deleter_holder<Derived> *m_del;
};

/**********************************************************************************************************************/

#endif // __shared_state_server__intrusive_base_hpp__included

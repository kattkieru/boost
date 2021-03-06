
//          Copyright Oliver Kowalke 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "boost/fiber/timed_mutex.hpp"

#include <algorithm>
#include <functional>

#include "boost/fiber/exceptions.hpp"
#include "boost/fiber/scheduler.hpp"

#ifdef BOOST_HAS_ABI_HEADERS
#  include BOOST_ABI_PREFIX
#endif

namespace boost {
namespace fibers {

bool
timed_mutex::try_lock_until_( std::chrono::steady_clock::time_point const& timeout_time) noexcept {
    if ( std::chrono::steady_clock::now() > timeout_time) {
        return false;
    }
    context * active_ctx = context::active();
    // store this fiber in order to be notified later
    detail::spinlock_lock lk{ wait_queue_splk_ };
    if ( nullptr == owner_) {
        owner_ = active_ctx;
        return true;
    }
    BOOST_ASSERT( ! active_ctx->wait_is_linked() );
    active_ctx->wait_link( wait_queue_);
    // suspend this fiber until notified or timed-out
    if ( ! active_ctx->wait_until( timeout_time, lk) ) {
        // remove fiber from wait-queue 
        lk.lock();
        wait_queue_.remove( * active_ctx);
        return false;
    }
    BOOST_ASSERT( ! active_ctx->wait_is_linked() );
    return true;
}

void
timed_mutex::lock() {
    context * active_ctx = context::active();
    // store this fiber in order to be notified later
    detail::spinlock_lock lk{ wait_queue_splk_ };
    if ( active_ctx == owner_) {
        throw lock_error{
                std::make_error_code( std::errc::resource_deadlock_would_occur),
                "boost fiber: a deadlock is detected" };
    } else if ( nullptr == owner_) {
        owner_ = active_ctx;
        return;
    }
    BOOST_ASSERT( ! active_ctx->wait_is_linked() );
    active_ctx->wait_link( wait_queue_);
    // suspend this fiber
    active_ctx->suspend( lk);
    BOOST_ASSERT( ! active_ctx->wait_is_linked() );
}

bool
timed_mutex::try_lock() {
    context * active_ctx = context::active();
    detail::spinlock_lock lk{ wait_queue_splk_ };
    if ( active_ctx == owner_) {
        throw lock_error{
                std::make_error_code( std::errc::resource_deadlock_would_occur),
                "boost fiber: a deadlock is detected" };
    } else if ( nullptr == owner_) {
        owner_ = active_ctx;
    }
    lk.unlock();
    // let other fiber release the lock
    active_ctx->yield();
    return active_ctx == owner_;
}

void
timed_mutex::unlock() {
    context * active_ctx = context::active();
    detail::spinlock_lock lk{ wait_queue_splk_ };
    if ( active_ctx != owner_) {
        throw lock_error{
                std::make_error_code( std::errc::operation_not_permitted),
                "boost fiber: no  privilege to perform the operation" };
    }
    if ( ! wait_queue_.empty() ) {
        context * ctx = & wait_queue_.front();
        wait_queue_.pop_front();
        owner_ = ctx;
        active_ctx->schedule( ctx);
    } else {
        owner_ = nullptr;
        return;
    }
}

}}

#ifdef BOOST_HAS_ABI_HEADERS
#  include BOOST_ABI_SUFFIX
#endif

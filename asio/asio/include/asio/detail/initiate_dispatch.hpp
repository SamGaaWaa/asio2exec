//
// detail/initiate_dispatch.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2023 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_INITIATE_DISPATCH_HPP
#define ASIO_DETAIL_INITIATE_DISPATCH_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"
#include "asio/associated_allocator.hpp"
#include "asio/associated_executor.hpp"
#include "asio/detail/work_dispatcher.hpp"
#include "asio/execution/allocator.hpp"
#include "asio/execution/blocking.hpp"
#include "asio/prefer.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

class initiate_dispatch
{
public:
  template <typename CompletionHandler>
  void operator()(ASIO_MOVE_ARG(CompletionHandler) handler,
      typename enable_if<
        execution::is_executor<
          typename associated_executor<
            typename decay<CompletionHandler>::type
          >::type
        >::value
      >::type* = 0) const
  {
    typedef typename decay<CompletionHandler>::type handler_t;

    typename associated_executor<handler_t>::type ex(
        (get_associated_executor)(handler));

    typename associated_allocator<handler_t>::type alloc(
        (get_associated_allocator)(handler));

#if defined(ASIO_NO_DEPRECATED)
    asio::prefer(ex, execution::allocator(alloc)).execute(
        asio::detail::bind_handler(
          ASIO_MOVE_CAST(CompletionHandler)(handler)));
#else // defined(ASIO_NO_DEPRECATED)
    execution::execute(
        asio::prefer(ex, execution::allocator(alloc)),
        asio::detail::bind_handler(
          ASIO_MOVE_CAST(CompletionHandler)(handler)));
#endif // defined(ASIO_NO_DEPRECATED)
  }

  template <typename CompletionHandler>
  void operator()(ASIO_MOVE_ARG(CompletionHandler) handler,
      typename enable_if<
        !execution::is_executor<
          typename associated_executor<
            typename decay<CompletionHandler>::type
          >::type
        >::value
      >::type* = 0) const
  {
    typedef typename decay<CompletionHandler>::type handler_t;

    typename associated_executor<handler_t>::type ex(
        (get_associated_executor)(handler));

    typename associated_allocator<handler_t>::type alloc(
        (get_associated_allocator)(handler));

    ex.dispatch(asio::detail::bind_handler(
          ASIO_MOVE_CAST(CompletionHandler)(handler)), alloc);
  }
};

template <typename Executor>
class initiate_dispatch_with_executor
{
public:
  typedef Executor executor_type;

  explicit initiate_dispatch_with_executor(const Executor& ex)
    : ex_(ex)
  {
  }

  executor_type get_executor() const ASIO_NOEXCEPT
  {
    return ex_;
  }

  template <typename CompletionHandler>
  void operator()(ASIO_MOVE_ARG(CompletionHandler) handler,
      typename enable_if<
        execution::is_executor<
          typename conditional<true, executor_type, CompletionHandler>::type
        >::value
      >::type* = 0,
      typename enable_if<
        !detail::is_work_dispatcher_required<
          typename decay<CompletionHandler>::type,
          Executor
        >::value
      >::type* = 0) const
  {
    typedef typename decay<CompletionHandler>::type handler_t;

    typename associated_allocator<handler_t>::type alloc(
        (get_associated_allocator)(handler));

#if defined(ASIO_NO_DEPRECATED)
    asio::prefer(ex_, execution::allocator(alloc)).execute(
        asio::detail::bind_handler(
          ASIO_MOVE_CAST(CompletionHandler)(handler)));
#else // defined(ASIO_NO_DEPRECATED)
    execution::execute(
        asio::prefer(ex_, execution::allocator(alloc)),
        asio::detail::bind_handler(
          ASIO_MOVE_CAST(CompletionHandler)(handler)));
#endif // defined(ASIO_NO_DEPRECATED)
  }

  template <typename CompletionHandler>
  void operator()(ASIO_MOVE_ARG(CompletionHandler) handler,
      typename enable_if<
        execution::is_executor<
          typename conditional<true, executor_type, CompletionHandler>::type
        >::value
      >::type* = 0,
      typename enable_if<
        detail::is_work_dispatcher_required<
          typename decay<CompletionHandler>::type,
          Executor
        >::value
      >::type* = 0) const
  {
    typedef typename decay<CompletionHandler>::type handler_t;

    typedef typename associated_executor<
      handler_t, Executor>::type handler_ex_t;
    handler_ex_t handler_ex((get_associated_executor)(handler, ex_));

    typename associated_allocator<handler_t>::type alloc(
        (get_associated_allocator)(handler));

#if defined(ASIO_NO_DEPRECATED)
    asio::prefer(ex_, execution::allocator(alloc)).execute(
        detail::work_dispatcher<handler_t, handler_ex_t>(
          ASIO_MOVE_CAST(CompletionHandler)(handler), handler_ex));
#else // defined(ASIO_NO_DEPRECATED)
    execution::execute(
        asio::prefer(ex_, execution::allocator(alloc)),
        detail::work_dispatcher<handler_t, handler_ex_t>(
          ASIO_MOVE_CAST(CompletionHandler)(handler), handler_ex));
#endif // defined(ASIO_NO_DEPRECATED)
  }

  template <typename CompletionHandler>
  void operator()(ASIO_MOVE_ARG(CompletionHandler) handler,
      typename enable_if<
        !execution::is_executor<
          typename conditional<true, executor_type, CompletionHandler>::type
        >::value
      >::type* = 0,
      typename enable_if<
        !detail::is_work_dispatcher_required<
          typename decay<CompletionHandler>::type,
          Executor
        >::value
      >::type* = 0) const
  {
    typedef typename decay<CompletionHandler>::type handler_t;

    typename associated_allocator<handler_t>::type alloc(
        (get_associated_allocator)(handler));

    ex_.dispatch(asio::detail::bind_handler(
          ASIO_MOVE_CAST(CompletionHandler)(handler)), alloc);
  }

  template <typename CompletionHandler>
  void operator()(ASIO_MOVE_ARG(CompletionHandler) handler,
      typename enable_if<
        !execution::is_executor<
          typename conditional<true, executor_type, CompletionHandler>::type
        >::value
      >::type* = 0,
      typename enable_if<
        detail::is_work_dispatcher_required<
          typename decay<CompletionHandler>::type,
          Executor
        >::value
      >::type* = 0) const
  {
    typedef typename decay<CompletionHandler>::type handler_t;

    typedef typename associated_executor<
      handler_t, Executor>::type handler_ex_t;
    handler_ex_t handler_ex((get_associated_executor)(handler, ex_));

    typename associated_allocator<handler_t>::type alloc(
        (get_associated_allocator)(handler));

    ex_.dispatch(detail::work_dispatcher<handler_t, handler_ex_t>(
          ASIO_MOVE_CAST(CompletionHandler)(handler),
          handler_ex), alloc);
  }

private:
  Executor ex_;
};

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_DETAIL_INITIATE_DISPATCH_HPP

/*
  Copyright (c) 2024 SamGaaWaa

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#pragma once

#if !defined(ASIO_TO_EXEC_USE_BOOST)
#include "asio/async_result.hpp"
#include "asio/error_code.hpp"
#include "asio/io_context.hpp"
#include "asio/cancellation_signal.hpp"
#include "asio/associated_executor.hpp"
#include "asio/post.hpp"
#else
#include "boost/asio/async_result.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/asio/cancellation_signal.hpp"
#include "boost/asio/associated_executor.hpp"
#include "boost/asio/post.hpp"
#endif

#include "stdexec/execution.hpp"

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <optional>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace asio2exec {

namespace __ex = stdexec;
#if !defined(ASIO_TO_EXEC_USE_BOOST)
namespace __io = asio;
#else
namespace __io = boost::asio;
#endif

namespace __detail{ 
    struct scheduler_t; 
}

class asio_context {
    friend struct __detail::scheduler_t;
public:
    using scheduler_t = __detail::scheduler_t;
    
    asio_context():
        _self{std::in_place},
        _ctx{*_self},
        _guard{std::in_place, __io::make_work_guard(_ctx) } 
    {}

    asio_context(__io::io_context& ctx):
        _ctx{ctx}
    {}

    asio_context(const asio_context&) = delete;
    asio_context(asio_context&&) = delete;
    asio_context& operator=(const asio_context&) = delete;
    asio_context& operator=(asio_context&&) = delete;

    ~asio_context() {
        join();
    }

    void start() {
        _th = std::thread([this] {
            _ctx.run();
        });
    }

    void stop()noexcept {
        _guard.reset();
    }

    void join(){
        stop();
        if(_th.joinable())
            _th.join();
    }

    __detail::scheduler_t get_scheduler()noexcept;

    __io::io_context& get_executor()noexcept { return _ctx; }
    const __io::io_context& get_executor()const noexcept { return _ctx; }

private:
    std::optional<__io::io_context> _self{};
    __io::io_context &_ctx;
    std::optional<__io::executor_work_guard<__io::io_context::executor_type>> _guard{};
    std::thread _th{};
};

struct use_sender_t 
{
    constexpr use_sender_t() {}

    template<class InnerExecutor>
    struct executor_with_default : InnerExecutor 
    {
        using default_completion_token_type = use_sender_t;

        executor_with_default(const InnerExecutor& ex)noexcept : InnerExecutor(ex) {}

        template<class _InnerExecutor>
            requires (!std::is_same_v<_InnerExecutor, executor_with_default> && std::is_convertible_v<_InnerExecutor, InnerExecutor>)
        executor_with_default(const _InnerExecutor& ex)noexcept : InnerExecutor(ex) {}
    };

    template<class T>
    using as_default_on_t = typename T::template rebind_executor<
        executor_with_default<typename T::executor_type>
    >::other;

    template<class T>
    static typename std::decay_t<T>::template rebind_executor<
        executor_with_default<typename std::decay_t<T>::executor_type>
    >::other
        as_default_on(T&& obj) 
    {
        return typename std::decay_t<T>::template rebind_executor<
            executor_with_default<typename std::decay_t<T>::executor_type>
        >::other(std::forward<T>(obj));
    }
};

inline constexpr use_sender_t use_sender{};

namespace __detail {

template<size_t Size = 64ull, size_t Alignment = alignof(std::max_align_t)>
class __sbo_buffer final: public std::pmr::memory_resource {
public:
    explicit __sbo_buffer(std::pmr::memory_resource* upstream =  std::pmr::get_default_resource())noexcept:
        _upstream{upstream}
    {}

    __sbo_buffer(const __sbo_buffer&)=delete;
    __sbo_buffer& operator=(const __sbo_buffer&)=delete;
    __sbo_buffer(__sbo_buffer&&)=delete;
    __sbo_buffer& operator=(__sbo_buffer&&)=delete;

private:
    void* do_allocate(size_t bytes, size_t alignment) override{
        if(_used || bytes > Size || alignment > Alignment){
            assert(_upstream && "Upstream memory_resource is empty.");
            return _upstream->allocate(bytes, alignment);
        }
        _used = true;
        return &_storage;
    }

    void do_deallocate(void* ptr, size_t bytes, size_t alignment)noexcept override {
        if(ptr == &_storage){
            _used = false;
            return;
        }
        _upstream->deallocate(ptr, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override{
        return this == std::addressof(other);
    }

private:
    std::pmr::memory_resource *_upstream;
    bool _used = false;
    alignas(Alignment) unsigned char _storage[Size];
};

struct scheduler_t {
    explicit scheduler_t(__io::io_context& ctx)noexcept:
        _ctx{&ctx}
    {}

    bool operator==(const scheduler_t&)const noexcept = default;

    auto schedule() const noexcept {
        return __schedule_sender_t{ _ctx };
    }
private:
    struct __schedule_sender_t {
        using sender_concept = __ex::sender_t;
        using completion_signatures = __ex::completion_signatures<
            __ex::set_value_t(),
            __ex::set_error_t(std::exception_ptr),
            __ex::set_stopped_t()
        >;

        __io::io_context* _ctx;

        struct __env_t {
            __io::io_context* _ctx;
            template<class CPO>
            auto query(__ex::get_completion_scheduler_t<CPO>) const noexcept {
                return scheduler_t{ *_ctx };
            }
        };
        
        template<__ex::receiver R>
        struct __op {
            __io::io_context* _ctx;
            R _r;

            template<__ex::receiver _R>
            __op(__io::io_context* ctx, _R&& r)noexcept:
                _ctx{ ctx },
                _r{ std::forward<_R>(r) }
            {}

            __op(const __op&) = delete;
            __op(__op&&) = delete;
            __op& operator=(const __op&) = delete;
            __op& operator=(__op&&) = delete;

            struct __sched_task_t {
                __op *self;
                using allocator_type = std::pmr::polymorphic_allocator<>;
                allocator_type allocator;
                allocator_type get_allocator() const noexcept { return allocator; }
                void operator()()noexcept{
                    __ex::set_value(std::move(self->_r));
                }
            };

            __sbo_buffer<128> _buf{};

            void start() & noexcept{
                if constexpr(!__ex::unstoppable_token<__ex::stop_token_of_t<__ex::env_of_t<R>>>){
                    const __ex::stoppable_token auto st = __ex::get_stop_token(__ex::get_env(_r));
                    if(st.stop_requested()){
                        __ex::set_stopped(std::move(_r));
                        return;
                    }
                }
                try{
                    __io::post(*_ctx,__sched_task_t{
                        .self{this},
                        .allocator{&_buf}
                    });
                }
                catch (...) {
                    __ex::set_error(std::move(_r), std::current_exception());
                }
            }
        };

        template<__ex::receiver R>
        auto connect(R&& r) noexcept {
            return __op<std::decay_t<R>>{ _ctx, std::forward<R>(r) };
        }

        __env_t get_env() const noexcept {
            return __env_t{ _ctx };
        }

    };

    __io::io_context* _ctx;
};

template<class ...Args>
struct __op_base{
    __op_base() = default;
    __op_base(const __op_base&) = delete;
    __op_base(__op_base&&) = delete;
    __op_base& operator=(const __op_base&) = delete;
    __op_base& operator=(__op_base&&) = delete;
    virtual void complete(Args ...args)noexcept{};
};  
    
template<class ...Args>
struct use_sender_handler_base {
    using allocator_type = std::pmr::polymorphic_allocator<>;

    __op_base<Args...>* op;
    allocator_type allocator;

    allocator_type get_allocator() const noexcept { return allocator; }

    template<class ..._Args>
    void operator()(_Args&& ...args) {
        op->complete(std::forward<_Args>(args)...);
    }
};

template<class ...Args>
struct use_sender_handler: use_sender_handler_base<Args...> {
    using cancellation_slot_type = __io::cancellation_slot;
    cancellation_slot_type slot;
    cancellation_slot_type get_cancellation_slot() const noexcept { return slot; }
};

// Copyright Ruslan Arutyunyan, 2019-2021.
// Copyright Antony Polukhin, 2021-2023.
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

// Contributed by Ruslan Arutyunyan

template <std::size_t OptimizeForSize, std::size_t OptimizeForAlignment>
class basic_any{
    static_assert(OptimizeForSize > 0 && OptimizeForAlignment > 0, "Size and Align shall be positive values");
    static_assert(OptimizeForSize >= OptimizeForAlignment, "Size shall non less than Align");
    static_assert((OptimizeForAlignment & (OptimizeForAlignment - 1)) == 0, "Align shall be a power of 2");
    static_assert(OptimizeForSize % OptimizeForAlignment == 0, "Size shall be multiple of alignment");
private:
    /// @cond
    enum operation{
        Destroy,
        Move,
        UnsafeCast
    };

    template <typename ValueType>
    static void* small_manager(operation op, basic_any& left, const basic_any* right){
        switch (op)
        {
            case Destroy:
                reinterpret_cast<ValueType*>(&left.content.small_value)->~ValueType();
                break;
            case Move: {
                ValueType* value = reinterpret_cast<ValueType*>(&const_cast<basic_any*>(right)->content.small_value);
                new (&left.content.small_value) ValueType(std::move(*value));
                left.man = right->man;
                reinterpret_cast<ValueType const*>(&right->content.small_value)->~ValueType();
                const_cast<basic_any*>(right)->man = 0;
            };
                break;
            case UnsafeCast:
                return reinterpret_cast<typename std::remove_cv<ValueType>::type *>(&left.content.small_value);
        }
        return 0;
    }

    template <typename ValueType>
    static void* large_manager(operation op, basic_any& left, const basic_any* right){
        switch (op)
        {
            case Destroy:
                delete static_cast<ValueType*>(left.content.large_value);
                break;
            case Move:
                left.content.large_value = right->content.large_value;
                left.man = right->man;
                const_cast<basic_any*>(right)->content.large_value = 0;
                const_cast<basic_any*>(right)->man = 0;
                break;
            case UnsafeCast:
                return reinterpret_cast<typename std::remove_cv<ValueType>::type *>(left.content.large_value);
        }
        return 0;
    }

    template <typename ValueType>
    struct is_small_object : std::integral_constant<bool, sizeof(ValueType) <= OptimizeForSize &&
        alignof(ValueType) <= OptimizeForAlignment &&
        std::is_nothrow_move_constructible<ValueType>::value>
    {};

    template <typename ValueType>
    static void create(basic_any& any, ValueType&& value, std::true_type){
        typedef typename std::decay<const ValueType>::type DecayedType;
        any.man = &small_manager<DecayedType>;
        new (&any.content.small_value) DecayedType(std::forward<ValueType>(value));
    }

    template <typename ValueType>
    static void create(basic_any& any, ValueType&& value, std::false_type){
        typedef typename std::decay<const ValueType>::type DecayedType;
        any.man = &large_manager<DecayedType>;
        any.content.large_value = new DecayedType(std::forward<ValueType>(value));
    }
    /// @endcond

public: // non-type template parameters accessors
        static constexpr std::size_t buffer_size = OptimizeForSize;
        static constexpr std::size_t buffer_align = OptimizeForAlignment;

public: // structors

    /// \post this->empty() is true.
    constexpr basic_any() noexcept
        : man(0), content()
    {
    }

    basic_any(basic_any&& other) noexcept
        : man(0), content()
    {
        if (other.man){
            other.man(Move, *this, &other);
        }
    }

    template<typename ValueType>
    basic_any(ValueType&& value
        , typename std::enable_if<!std::is_same<basic_any&, ValueType>::value >::type* = 0 // disable if value has type `basic_any&`
        , typename std::enable_if<!std::is_const<ValueType>::value >::type* = 0) // disable if value has type `const ValueType&&`
        : man(0), content(){
        typedef typename std::decay<ValueType>::type DecayedType;
        static_assert(
            !std::is_same<DecayedType, basic_any>::value,
            "basic_any shall not be constructed from basic_any"
        );
        create(*this, static_cast<ValueType&&>(value), is_small_object<DecayedType>());
    }

    ~basic_any() noexcept{
        if (man){
            man(Destroy, *this, 0);
        }
    }

public: // modifiers
    basic_any & swap(basic_any & rhs) noexcept{
        if (this == &rhs){
            return *this;
        }

        if (man && rhs.man){
            basic_any tmp;
            rhs.man(Move, tmp, &rhs);
            man(Move, rhs, this);
            tmp.man(Move, *this, &tmp);
        }
        else if (man){
            man(Move, rhs, this);
        }
        else if (rhs.man){
            rhs.man(Move, *this, &rhs);
        }
        return *this;
    }

    basic_any & operator=(basic_any&& rhs) noexcept{
        rhs.swap(*this);
        basic_any().swap(rhs);
        return *this;
    }

    template <class ValueType>
    basic_any & operator=(ValueType&& rhs){
        typedef typename std::decay<ValueType>::type DecayedType;
        static_assert(
            !std::is_same<DecayedType, basic_any>::value,
            "basic_any shall not be assigned into basic_any"
        );
        basic_any(std::forward<ValueType>(rhs)).swap(*this);
        return *this;
    }

public: // queries
    bool empty() const noexcept{
        return !man;
    }

    /// \post this->empty() is true
    void clear() noexcept{
        basic_any().swap(*this);
    }

private: // representation

    template<typename ValueType, std::size_t Size, std::size_t Alignment>
    friend ValueType * unsafe_any_cast(basic_any<Size, Alignment> *) noexcept;

    typedef void*(*manager)(operation op, basic_any& left, const basic_any* right);

    manager man;

    union content {
        void * large_value;
        alignas(OptimizeForAlignment) unsigned char small_value[OptimizeForSize];
    } content;
    /// @endcond
};

/// Exchange of the contents of `lhs` and `rhs`.
/// \throws Nothing.
template<std::size_t OptimizeForSize, std::size_t OptimizeForAlignment>
inline void swap(basic_any<OptimizeForSize, OptimizeForAlignment>& lhs, basic_any<OptimizeForSize, OptimizeForAlignment>& rhs) noexcept{
    lhs.swap(rhs);
}

template<typename ValueType, std::size_t OptimizedForSize, std::size_t OptimizeForAlignment>
inline ValueType * unsafe_any_cast(basic_any<OptimizedForSize, OptimizeForAlignment> * operand) noexcept{
    return static_cast<ValueType*>(operand->man(basic_any<OptimizedForSize, OptimizeForAlignment>::UnsafeCast, *operand, 0));
}

template<typename ValueType, std::size_t OptimizeForSize, std::size_t OptimizeForAlignment>
inline const ValueType * unsafe_any_cast(const basic_any<OptimizeForSize, OptimizeForAlignment> * operand) noexcept{
    return unsafe_any_cast<ValueType>(const_cast<basic_any<OptimizeForSize, OptimizeForAlignment> *>(operand));
}

template<class ...Args>
struct __initializer{
    using __any_t = basic_any<256, alignof(std::max_align_t)>;
private:
    struct __init_base{
        virtual void init(use_sender_handler_base<Args...>&&) = 0;
        virtual void init(use_sender_handler<Args...>&&) = 0;
    };

    template<class Init, class ...InitArgs>
    struct __init_impl final: __init_base{
        Init _init;
        std::tuple<InitArgs...> _args;

        template<class _Init, class ..._InitArgs>
        __init_impl(_Init&& init, _InitArgs&& ...args): 
            _init{std::forward<_Init>(init)}, 
            _args{std::forward<_InitArgs>(args)...}
        {}

        __init_impl(const __init_impl&) = delete;
        __init_impl& operator=(const __init_impl&) = delete;
        __init_impl(__init_impl&&)noexcept = default;
        __init_impl& operator=(__init_impl&&)noexcept = default;

        void init(use_sender_handler_base<Args...>&& handler) override{
            std::apply(std::move(_init), std::tuple_cat(std::make_tuple(std::move(handler)), std::move(_args)));
        }

        void init(use_sender_handler<Args...>&& handler) override{
            std::apply(std::move(_init), std::tuple_cat(std::make_tuple(std::move(handler)), std::move(_args)));
        }
    };
public:
    template<class Init, class ...InitArgs>
    __initializer(Init&& init, InitArgs&& ...args){
        _data = __init_impl<std::decay_t<Init>, std::decay_t<InitArgs>...>(std::forward<Init>(init), std::forward<InitArgs>(args)...);
    }

    template<class Init>
    __initializer(Init&& init){
        _data = __init_impl<std::decay_t<Init>>(std::forward<Init>(init));
    }

    __initializer(const __initializer&) = delete;
    __initializer& operator=(const __initializer&) = delete;
    
    __initializer(__initializer&& other)noexcept:
        _data{std::move(other._data)}
    {}

    void operator()(use_sender_handler_base<Args...>&& handler){
        unsafe_any_cast<__init_base>(&_data)->init(std::move(handler));
    }

    void operator()(use_sender_handler<Args...>&& handler){
        unsafe_any_cast<__init_base>(&_data)->init(std::move(handler));
    }
private:
    __any_t _data;
};

template<class T>
concept __is_tuple = requires (T&& t){
    std::get<0>(t);
}; 

template<class T>
constexpr decltype(auto) __unwrap_first(T&& t)noexcept{
    return std::forward<T>(t);
}

template<__is_tuple T>
constexpr decltype(auto) __unwrap_first(T&& t)noexcept{
    return __unwrap_first(std::get<0>(std::forward<T>(t)));
}

template<class T>
using __unwrap_first_t = std::decay_t<decltype(__unwrap_first(std::declval<T>()))>;

template<__is_tuple T>
constexpr decltype(auto) __unwrap_tuple(T&& t)noexcept{
    if constexpr(std::tuple_size<T>{} == size_t(1)){
        return __unwrap_tuple(std::get<0>(std::forward<T>(t)));
    }else{
        return std::forward<T>(t);
    }
}

template<class T>
constexpr decltype(auto) __unwrap_tuple(T&& t)noexcept{
    return std::make_tuple(std::forward<T>(t));
}

template<class ...Args>
struct __sender{
    using sender_concept = __ex::sender_t;
    using completion_signatures = __ex::completion_signatures<
        __ex::set_value_t(Args...),
        __ex::set_error_t(std::exception_ptr),
        __ex::set_stopped_t()
    >;
    __initializer<Args...> _init;

    template<__ex::receiver R>
    struct __operation_base: __op_base<Args...> {
        using __storage_t = std::variant<
            __initializer<Args...>, 
            __sbo_buffer<512>
        >;

        __storage_t _storage;
        R _r;

        __operation_base(__initializer<Args...>&& i, R&& r):
            _storage{std::move(i)}, _r{std::move(r)}
        {}

        __initializer<Args...>& __get_initializer()noexcept{
            return std::get<0>(_storage);
        }

        __sbo_buffer<512>& __emplace_buffer()noexcept{
            return _storage.template emplace<1>();
        }

        void __stop()noexcept{
            __ex::set_stopped(std::move(_r));
        }

        void __error()noexcept{
            __ex::set_error(std::move(_r), std::current_exception());
        }

        void __init(){
            auto initializer{std::move(__get_initializer())};
            std::move(initializer)(use_sender_handler_base<Args...>{
                .op{this},
                .allocator{&__emplace_buffer()}
            });
        }

        void complete(Args ...args)noexcept override{
            const auto& res = std::tie(args...);
            const auto& may_be_ec = __unwrap_first(res);
            if constexpr(requires { may_be_ec == std::errc::operation_canceled; }){
                if(may_be_ec == std::errc::operation_canceled){
                    __stop();
                    return;
                }
            }
            __ex::set_value(std::move(_r), std::move(args)...);
        }
    };

    template<__ex::receiver R>
    struct __operation final: __operation_base<R> {
        __operation(__initializer<Args...>&& i, R&& r)
            : __operation_base<R>(std::move(i), std::move(r))
        {}

        enum struct __state_t: char{
            construction, emplaced, initiated, stopped
        };

        __io::cancellation_signal _signal{};
        std::atomic<__state_t> _state{__state_t::construction};

        struct __stop_t{
            __operation *self;
            void operator()()noexcept{
                __state_t expected = self->_state.load(std::memory_order_relaxed);
                while(!self->_state.compare_exchange_weak(expected, __state_t::stopped, std::memory_order_acq_rel))
                {}
                if(expected == __state_t::initiated){
                    self->_signal.emit(__io::cancellation_type_t::total);
                }
            }
        };

        using __stop_callback_t = typename __ex::stop_token_of_t<__ex::env_of_t<R>&>:: template callback_type<__stop_t>;
        std::optional<__stop_callback_t> _stop_callback{};

        void __init(){
            auto initializer{std::move(this->__get_initializer())};
            std::move(initializer)(use_sender_handler<Args...>{
                {
                    .op{this},
                    .allocator{&this->__emplace_buffer()}
                },
                _signal.slot()
            });
        }

        void complete(Args ...args)noexcept override{
            _stop_callback.reset();
            __operation_base<R>::complete(std::move(args)...);
        }

        void start() & noexcept
        {
            const auto st = __ex::get_stop_token(__ex::get_env(this->_r));
            if(st.stop_requested()){
                this->__stop();
                return;
            }
            _stop_callback.emplace(st, __stop_t{this});
            __state_t expected = __state_t::construction;
            if(!_state.compare_exchange_strong(expected, __state_t::emplaced, std::memory_order_acq_rel)){
                _stop_callback.reset();
                this->__stop();
                return;
            }
            //初始化IO
            try{
                this->__init();
            }catch(...){
                _stop_callback.reset();
                this->__error();
                return;
            }
            // 如果没有请求取消，self._state == __state_t::emplaced
            expected = __state_t::emplaced;
            if(!_state.compare_exchange_strong(expected, __state_t::initiated, std::memory_order_acq_rel)){
                // 已经请求取消，但stop_callback不会发出取消信号（见__stop_t的if分支）
                _stop_callback.reset();
                _signal.emit(__io::cancellation_type_t::total);
                return;
            }
        }
    };

    template<__ex::receiver R>
    struct __asio_op_without_cancellation final: __operation_base<R> {
        __asio_op_without_cancellation(__initializer<Args...>&& i, R&& r)
            : __operation_base<R>(std::move(i), std::move(r))
        {}
        
        void start() & noexcept{
            try {
                this->__init();
            }
            catch (...) {
                this->__error();
            }
        }
    };

    struct __transfer_sender {
        using sender_concept = __ex::sender_t;
        using completion_signatures = __ex::completion_signatures<
            __ex::set_value_t(Args...),
            __ex::set_error_t(std::exception_ptr),
            __ex::set_stopped_t()
        >;

        __initializer<Args...> _init;

        template<__ex::receiver R>
        struct __transfer_op_without_cancellation final: __operation_base<R> {
            __transfer_op_without_cancellation(__initializer<Args...>&& i, R&& r)
                : __operation_base<R>(std::move(i), std::move(r))
            {}

            void start() & noexcept
            {
                try {
                    this->__init();
                }
                catch (...) {
                    this->__error();
                }
            }
        };

        template<__ex::receiver R>
        __ex::operation_state auto connect(R&& r) && {
            if constexpr(__ex::unstoppable_token<__ex::stop_token_of_t<__ex::env_of_t<R>>>){
                return __transfer_op_without_cancellation<std::decay_t<R>>(
                    std::move(this->_init),
                    std::forward<R>(r)
                );
            }else{
                return __operation<std::decay_t<R>>(
                    std::move(this->_init),
                    std::forward<R>(r)
                );
            }
        }
    };

    template<__ex::receiver R>
    __ex::operation_state auto connect(R&& r) &&
    {
        auto env = __ex::get_env(r);
        if constexpr(requires { __ex::get_scheduler(env); }){
            return __ex::connect(
                __ex::continues_on(__transfer_sender{._init{std::move(this->_init)}}, __ex::get_scheduler(env)),
                std::forward<R>(r)
            );
        }else{
            if constexpr(__ex::unstoppable_token<__ex::stop_token_of_t<__ex::env_of_t<R>>>){
                return __asio_op_without_cancellation<std::decay_t<R>>(
                    std::move(this->_init),
                    std::forward<R>(r)
                );
            }else{
                return __operation<std::decay_t<R>>(
                    std::move(this->_init),
                    std::forward<R>(r)
                );
            }
        }
    }

}; // __sender

}// __detail

inline __detail::scheduler_t asio_context::get_scheduler()noexcept { return __detail::scheduler_t{ _ctx }; }

template<class ...Args>
using sender = __detail::__sender<Args...>;

using scheduler = __detail::scheduler_t;

static_assert(__ex::scheduler<scheduler>);

}// asio2exec

#if !defined(ASIO_TO_EXEC_USE_BOOST)
namespace asio{
#else
namespace boost::asio{
#endif
    template<class ...Args>
    struct async_result<asio2exec::use_sender_t, void(Args...)> {
        using return_type = asio2exec::sender<Args...>;

        template<class Initiation, class ...InitArgs>
        static return_type initiate(
            Initiation&& init,
            asio2exec::use_sender_t,
            InitArgs&& ...args
        ){
            return return_type{asio2exec::__detail::__initializer<Args...>(
                        std::forward<Initiation>(init),
                        std::forward<InitArgs>(args)...
                    )};
        }

    };
} // asio

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
#include "asio/bind_executor.hpp"
#include "asio/bind_allocator.hpp"
#include "asio/any_io_executor.hpp"
#else
#include "boost/asio/async_result.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/asio/cancellation_signal.hpp"
#include "boost/asio/associated_executor.hpp"
#include "boost/asio/post.hpp"
#include "boost/asio/bind_executor.hpp"
#include "boost/asio/bind_allocator.hpp"
#include "boost/asio/any_io_executor.hpp"
#endif

#include "stdexec/execution.hpp"

#include <atomic>
#include <cassert>
#include <cstring>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#define STDEXEC stdexec
#if !defined(ASIO_TO_EXEC_USE_BOOST)
#define ASIO asio
#else
#define ASIO boost::asio
#endif

namespace asio2exec {

namespace __detail{ struct scheduler_t; }

class asio_context {
    friend struct __detail::scheduler_t;
public:
    using scheduler_t = __detail::scheduler_t;
    
    asio_context() :
        _guard{ ASIO::make_work_guard(_ctx) } 
    {}

    asio_context(const asio_context&) = delete;
    asio_context(asio_context&&) = delete;
    asio_context& operator=(const asio_context&) = delete;
    asio_context& operator=(asio_context&&) = delete;

    ~asio_context() {
        stop();
        if(_th.joinable())
            _th.join();
    }

    void start() {
        _th = std::thread([this] {
            _ctx.run();
        });
    }

    void stop()noexcept {
        _guard.reset();
    }

    __detail::scheduler_t get_scheduler()noexcept;

    ASIO::io_context& get_executor()noexcept { return _ctx; }
    const ASIO::io_context& get_executor()const noexcept { return _ctx; }

private:
    ASIO::io_context _ctx;
    ASIO::executor_work_guard<ASIO::io_context::executor_type> _guard;
    std::thread _th;
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

template<size_t Size = 64ull, size_t Alignment = 16>
class __sbo_buffer: public std::pmr::memory_resource {
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
            assert(("Upstream memory_resource is empty.", _upstream));
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
    asio_context* _ctx;

    bool operator==(const scheduler_t&)const noexcept = default;

    struct __schedule_sender_t {
        using sender_concept = STDEXEC::sender_t;
        asio_context* _ctx;

        using completion_signatures = STDEXEC::completion_signatures<
            STDEXEC::set_value_t(),
            STDEXEC::set_error_t(std::exception_ptr),
            STDEXEC::set_stopped_t()
        >;

        struct __env_t {
            asio_context* _ctx;

            template<class CPO>
            friend auto tag_invoke(STDEXEC::get_completion_scheduler_t<CPO>, const __env_t& self)noexcept {
                return self._ctx->get_scheduler();
            }
        };
        
        template<STDEXEC::receiver R>
        struct __op {
            asio_context* _ctx;
            R _r;

            template<STDEXEC::receiver _R>
            __op(asio_context* ctx, _R&& r)noexcept:
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
                    STDEXEC::set_value(std::move(self->_r));
                }
            };

            __sbo_buffer<128> _buf{};

            friend void tag_invoke(STDEXEC::start_t, __op& self)noexcept{
                if constexpr(!STDEXEC::unstoppable_token<STDEXEC::stop_token_of_t<STDEXEC::env_of_t<R>>>){
                    const STDEXEC::stoppable_token auto st = STDEXEC::get_stop_token(STDEXEC::get_env(self._r));
                    if(st.stop_requested()){
                        STDEXEC::set_stopped(std::move(self._r));
                        return;
                    }
                }
                try{
                    self._ctx->get_executor().post(__sched_task_t{
                        .self{&self},
                        .allocator{&self._buf}
                    });
                }
                catch (...) {
                    STDEXEC::set_error(std::move(self._r), std::current_exception());
                }
            }
        };

        template<STDEXEC::receiver R>
        friend auto tag_invoke(STDEXEC::connect_t, __schedule_sender_t self, R&& r)noexcept {
            return __schedule_sender_t::__op<std::decay_t<R>>{ self._ctx, std::forward<R>(r) };
        }

        friend auto tag_invoke(STDEXEC::get_env_t, const __schedule_sender_t& self)noexcept {
            return __schedule_sender_t::__env_t{ self._ctx };
        }
    };

    friend auto tag_invoke(STDEXEC::schedule_t, scheduler_t self)noexcept {
        return __schedule_sender_t{ self._ctx };
    }
};

template<class ...Args>
struct __op_base{
    virtual void complete(Args ...args)noexcept{};
    virtual ~__op_base(){}
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
    using cancellation_slot_type = ASIO::cancellation_slot;
    cancellation_slot_type slot;
    cancellation_slot_type get_cancellation_slot() const noexcept { return slot; }
};

struct __any_t{
private:
    struct __any_base {
        using __move_to_fn = __any_base* (*)(__any_base*, void *buf, size_t size)noexcept;
        using __get_fn = void* (*)(__any_base*)noexcept;
        using __destroy_fn = void (*)(__any_base*)noexcept;
        using __free_fn = void (*)(__any_base*)noexcept;

        __move_to_fn move_to{nullptr};
        __get_fn get{nullptr};
        __destroy_fn destroy{nullptr};
        __free_fn free{nullptr};
        size_t size{0};
        size_t alignment{0};

        __any_base() = default;

        __any_base(const __any_base&) = delete;
        __any_base& operator=(const __any_base&) = delete;

        __any_base(__any_base&& other)noexcept{}
        __any_base& operator=(__any_base&& other)noexcept = default;
    };

    template<class T>
        requires std::is_nothrow_move_constructible_v<T>
    struct __any_impl: __any_base{
        union{
            T _val;
        };

        __any_impl()noexcept{
            move_to = __move_to;
            get = __get;
            destroy = __destroy;
            free = __free;
            size = sizeof(__any_impl);
            alignment = alignof(__any_impl);
        }

        template<class ..._Args>
        __any_impl(_Args&&... args)noexcept(std::is_nothrow_constructible_v<T, _Args...>):
            _val(std::forward<_Args>(args)...)
        {
            move_to = __move_to;
            get = __get;
            destroy = __destroy;
            free = __free;
            size = sizeof(__any_impl);
            alignment = alignof(__any_impl);
        }

        __any_impl(__any_impl&& other) = delete;
        __any_impl& operator=(__any_impl&& other) = delete;

        static __any_base *__move_to(__any_base* base, void *buf, size_t size)noexcept{
            auto self = (__any_impl*)base;
            auto impl = (__any_impl*)std::align(alignof(__any_impl), sizeof(__any_impl), buf, size);
            assert(impl);
            if constexpr(!std::is_trivially_move_constructible_v<T>){
                new (impl) __any_impl(std::move(self->_val));
            }else{
                new (impl) __any_impl;
                std::memcpy(std::addressof(impl->_val), std::addressof(self->_val), sizeof(T));
            }
            if constexpr(!std::is_trivially_destructible_v<T>){
                self->_val.~T();
            }
            return impl;
        }

        static void *__get(__any_base* base)noexcept{
            return std::addressof(((__any_impl*)base)->_val);
        }

        static void __destroy(__any_base* base)noexcept{
            if constexpr(!std::is_trivially_destructible_v<T>){
                auto self = (__any_impl*)base;
                self->_val.~T();
            }
        }

        static void __free(__any_base* base)noexcept{
            std::allocator<__any_impl> allocator;
            allocator.deallocate((__any_impl*)base, 1);
        }
    };

    template<class F>
        requires (std::is_nothrow_invocable_v<F> && 
                    std::is_nothrow_move_constructible_v<F>)
    struct __guard{
        F _f;
        bool _valid{true};

        explicit __guard(F&& f)noexcept:
            _f{std::move(f)}
        {}

        __guard(const __guard&) = delete;
        __guard& operator=(const __guard&) = delete;
        __guard(__guard&& other) = delete;
        __guard& operator=(__guard&& other) = delete;

        ~__guard()noexcept{
            if(_valid){
                _f();
            }
        }

        void reset()noexcept{
            _valid = false;
        }
    };
    
public:
    __any_t()noexcept = default;

    __any_t(const __any_t&) = delete;
    __any_t& operator=(const __any_t&) = delete;

    __any_t(__any_t&& other)noexcept{
        if(!other._ptr){
            return;
        }
        if(other.__is_allocated()){
            _ptr = std::exchange(other._ptr, nullptr);
            return;
        }
        _ptr = std::exchange(other._ptr, nullptr);
        _ptr = _ptr->move_to(_ptr, _storage, sizeof(_storage));
    }

    __any_t& operator=(__any_t&& other)noexcept{
        if(this == &other)
            return *this;
        if(!other._ptr){
            reset();
            return *this;
        }
        if(!_ptr){
            if(other.__is_allocated()){
                _ptr = std::exchange(other._ptr, nullptr);
                return *this;
            }
            _ptr = std::exchange(other._ptr, nullptr);
            _ptr = _ptr->move_to(_ptr, _storage, sizeof(_storage));
            return *this;
        }
        const size_t size = _ptr->size;
        const size_t alignment = _ptr->alignment;
        __destroy_uncheck();
        if(other.__is_allocated()){
            if(__is_allocated())
                __free_uncheck();
            _ptr = std::exchange(other._ptr, nullptr);
            return *this;
        }
        if(__is_allocated()){
            if(other._ptr->size > size || other._ptr->alignment > alignment){
                __free_uncheck();
                _ptr = std::exchange(other._ptr, nullptr);
                _ptr = _ptr->move_to(_ptr, _storage, sizeof(_storage));
                return *this;
            }
            void *heap_ptr = _ptr;
            const __any_base::__free_fn free_fn = _ptr->free;
            _ptr = std::exchange(other._ptr, nullptr);
            _ptr = _ptr->move_to(_ptr, heap_ptr, size);
            _ptr->free = free_fn;
            assert(_ptr == heap_ptr);
            return *this;
        }
        _ptr = std::exchange(other._ptr, nullptr);
        _ptr = _ptr->move_to(_ptr, _storage, sizeof(_storage));
        return *this;
    }

    ~__any_t(){
        reset();
    }

    template<class T>
        requires (!std::is_same_v<std::decay_t<T>, __any_t> && std::is_nothrow_move_constructible_v<T>)
    __any_t(T&& val){
        __emplace<T>(std::forward<T>(val));
    }

    template<class T>
        requires (!std::is_same_v<std::decay_t<T>, __any_t> && std::is_nothrow_move_constructible_v<T>)
    __any_t& operator=(T&& val){
        __emplace<T>(std::forward<T>(val));
        return *this;
    }

    template<class T, class ...Args>
        requires std::is_nothrow_move_constructible_v<T>
    std::decay_t<T>& emplace(Args&& ...args){
        return __emplace<T>(std::forward<Args>(args)...);
    }

    bool has_value()const noexcept{ 
        return _ptr != nullptr; 
    }

    void reset()noexcept{ 
        if(_ptr){
            __destroy_uncheck();
            if(__is_allocated())
                __free_uncheck();
            _ptr = nullptr;
        }
    }

    template<class T>
    T &get()& noexcept{
        return *((T*)_ptr->get(_ptr));
    }

    template<class T>
    const T &get()const& noexcept{
        return *((const T*)_ptr->get(_ptr));
    }

    template<class T>
    T&& get()&& noexcept{
        return std::move(*((T*)_ptr->get(_ptr)));
    }

    template<class T>
    const T&& get()const&& noexcept{
        return static_cast<const T&&>(*((const T*)_ptr->get(_ptr)));
    }

    friend void swap(__any_t& x, __any_t& y)noexcept{
        if(!x._ptr){
            if(!y._ptr)
                return;       
            if(y.__is_allocated()){
                x._ptr = std::exchange(y._ptr, nullptr);
                return;
            }
            x._ptr = std::exchange(y._ptr, nullptr);
            x._ptr = x._ptr->move_to(x._ptr, x._storage, sizeof(x._storage));
            return;
        }
        if(!y._ptr){
            return swap(y, x);
        }
        if(x.__is_allocated()){
            if(y.__is_allocated()){
                std::swap(x._ptr, y._ptr);
                return;
            }
            std::swap(x._ptr, y._ptr);
            x._ptr = x._ptr->move_to(x._ptr, x._storage, sizeof(x._storage));
            return;
        }
        if(y.__is_allocated()){
            return swap(y, x);
        }
        alignas(16) char tmp[128];
        auto tmp_ptr = x._ptr->move_to(x._ptr, tmp, sizeof(tmp));
        x._ptr = y._ptr->move_to(y._ptr, x._storage, sizeof(x._storage));
        y._ptr = tmp_ptr->move_to(tmp_ptr, y._storage, sizeof(y._storage));
    }

private:
    template<class T, class ...Args>
    std::decay_t<T>& __emplace(Args&& ...args){
        using impl_t = __any_impl<std::decay_t<T>>;
        if(!_ptr){
            void *impl = __get_align_ptr<T>(_storage, sizeof(_storage));
            if(!impl){
                impl = __allocate<T>();
            }
            _ptr = (__any_base*)impl;
            const __any_base::__free_fn free_fn = impl_t::__free; 
            __guard guard{[free_fn, this]()mutable noexcept{
                if(__is_allocated())
                    free_fn(_ptr);
                _ptr = nullptr;
            }};
            new (_ptr) impl_t(std::forward<Args>(args)...);
            guard.reset();
            return get<std::decay_t<T>>();
        }
        __destroy_uncheck();
        if(__is_allocated() &&
            sizeof(impl_t) <= _ptr->size &&
            alignof(impl_t) <= _ptr->alignment)
        {
            const __any_base::__free_fn free_fn = _ptr->free;
            __guard guard{[free_fn, this]()mutable noexcept{
                free_fn(_ptr);
                _ptr = nullptr;
            }};
            new (_ptr) impl_t(std::forward<Args>(args)...);
            _ptr->free = free_fn;
            guard.reset();
            return get<std::decay_t<T>>();
        }
        _ptr = nullptr;
        return __emplace<T>(std::forward<Args>(args)...);
    }

    bool __is_allocated()const noexcept{
        const auto ptr = (char*)_ptr;
        return ptr && !(_storage <= ptr && ptr < _storage + sizeof(_storage));
    }

    template<class T>
    static void *__allocate(){
        using impl_t = __any_impl<std::decay_t<T>>;
        std::allocator<impl_t> allocator;
        return allocator.allocate(1);
    }

    template<class T>
    static void *__get_align_ptr(void *buf, size_t size)noexcept{
        using impl_t = __any_impl<std::decay_t<T>>;
        return std::align(alignof(impl_t), sizeof(impl_t), buf, size);
    }

    void __destroy_uncheck()noexcept{
        _ptr->destroy(_ptr);
    }

    void __destroy()noexcept{
        if(_ptr)
            __destroy_uncheck();
    }

    void __free_uncheck()noexcept{
        _ptr->free(_ptr);
    }

    void __free()noexcept{
        if(__is_allocated())
            __free_uncheck();
        _ptr = nullptr;
    }

    __any_base *_ptr{nullptr};
    alignas(16) char _storage[128];
};

template<class ...Args>
struct __initializer{
private:
    struct __init_base{
        virtual ~__init_base()noexcept = default;
        virtual void init(use_sender_handler_base<Args...>&&) = 0;
        virtual void init(use_sender_handler<Args...>&&) = 0;
    };

    template<class Init, class ...InitArgs>
    struct __init_impl: __init_base{
        Init _init;
        std::tuple<InitArgs...> _args;

        __init_impl(Init init, InitArgs ...args): 
            _init{std::move(init)}, 
            _args{std::move(args)...}
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
    using executor_type = ASIO::any_io_executor;

    template<class Init, class ...InitArgs>
        requires std::is_convertible_v<typename ASIO::associated_executor<Init>::type, executor_type>
    __initializer(Init&& init, InitArgs&& ...args):
        _ex{ASIO::get_associated_executor(init)}
    {
        _data.emplace<__init_impl<std::decay_t<Init>, std::decay_t<InitArgs>...>>(std::forward<Init>(init), std::forward<InitArgs>(args)...);
    }

    template<class Init>
        requires std::is_convertible_v<typename ASIO::associated_executor<Init>::type, executor_type>
    __initializer(Init&& init):
        _ex{ASIO::get_associated_executor(init)}
    {
        _data.emplace<__init_impl<std::decay_t<Init>>>(std::forward<Init>(init));
    }

    __initializer(const __initializer&) = delete;
    __initializer& operator=(const __initializer&) = delete;
    
    __initializer(__initializer&& other)noexcept:
        _data{std::move(other._data)},
        _ex{other._ex}
    {}

    void operator()(use_sender_handler_base<Args...>&& handler){
        _data.get<__init_base>().init(std::move(handler));
    }

    void operator()(use_sender_handler<Args...>&& handler){
        _data.get<__init_base>().init(std::move(handler));
    }

    const executor_type& get_executor()const noexcept{ return _ex; }
    executor_type& get_executor()noexcept{ return _ex; }
private:
    __any_t _data;
    executor_type _ex;
};

template<class ...Args>
consteval size_t __get_args_size(){
    if constexpr((sizeof ...(Args)) == 0)
        return 0ull;
    else return (sizeof(Args) + ...);
}

template<class T>
concept __is_tuple = requires (T&& t){
    std::get<0>(t);
}; 

template<class T>
constexpr auto& __unwrap_first(T& t)noexcept{
    return t;
}

template<__is_tuple T>
constexpr auto& __unwrap_first(T& t)noexcept{
    using __first_t = typename std::tuple_element<0, T>::type;
    return __unwrap_first<__first_t>(std::get<0>(t));
}

template<class T>
using __unwrap_first_t = std::decay_t<decltype(__unwrap_first(std::declval<T>()))>;

template<__is_tuple T>
constexpr auto&& __unwrap_tuple(T&& t)noexcept{
    if constexpr(std::tuple_size<T>{} == size_t(1)){
        return __unwrap_tuple(std::get<0>(std::forward<T>(t)));
    }else{
        return std::forward<T>(t);
    }
}

template<class T>
constexpr auto&& __unwrap_tuple(T&& t)noexcept{
    return std::make_tuple(std::forward<T>(t));
}

template <class _Fn>
    requires std::is_nothrow_move_constructible_v<_Fn>
struct __conv {
    _Fn __fn_;
    using __t = std::invoke_result_t<_Fn>;

    operator __t() && noexcept(std::is_nothrow_invocable_v<_Fn>) {
        return ((_Fn&&) __fn_)();
    }

    __t operator()() && noexcept(std::is_nothrow_invocable_v<_Fn>) {
        return ((_Fn&&) __fn_)();
    }
};

template <class _Fn>
__conv(_Fn) -> __conv<_Fn>;

template<class T>
constexpr std::string_view __type_name(){
    return {__PRETTY_FUNCTION__};
}

template<class ...Args>
struct __sender{
    using sender_concept = STDEXEC::sender_t;

    __initializer<Args...> _init;

    template<STDEXEC::receiver R>
    struct __operation_base: asio2exec::__detail::__op_base<Args...> {
        using __res_t = std::optional<std::tuple<Args...>>;

        __initializer<Args...> _init;
        R _r;
        asio2exec::asio_context *_ctx{nullptr};
        __res_t _res{};
        std::exception_ptr _err{};

        __operation_base(__initializer<Args...>&& i, R&& r, asio2exec::asio_context *ctx = nullptr)
            : _init{std::move(i)}, _r{std::move(r)}, _ctx{ctx}
        {}

        __operation_base(const __operation_base&) = delete;
        __operation_base(__operation_base&&) = delete;
        __operation_base& operator=(const __operation_base&) = delete;
        __operation_base& operator=(__operation_base&&) = delete;

        template<class Tag>
        struct __completion_task_t {
            using allocator_type = std::pmr::polymorphic_allocator<>;

            __operation_base *self;
            allocator_type allocator;

            allocator_type get_allocator() const noexcept { return allocator; }

            void operator()()noexcept{
                if constexpr(std::is_same_v<Tag, STDEXEC::set_value_t>){
                    std::apply(STDEXEC::set_value, std::tuple_cat(std::make_tuple(std::move(self->_r)), std::move(*self->_res)));
                }else if constexpr(std::is_same_v<Tag, STDEXEC::set_error_t>){
                    STDEXEC::set_error(std::move(self->_r), std::move(self->_err));
                }else if constexpr(std::is_same_v<Tag, STDEXEC::set_stopped_t>){
                    STDEXEC::set_stopped(std::move(self->_r));
                }else{
                    std::terminate();
                }
            }
        };

        asio2exec::__detail::__sbo_buffer<128> _buf{};

        void __value()noexcept{
            if(_ctx){
                ASIO::post(_ctx->get_executor().get_executor(), __completion_task_t<STDEXEC::set_value_t>{
                    .self{this},
                    .allocator{&_buf}
                });
                return;
            }
            std::apply(STDEXEC::set_value, std::tuple_cat(std::make_tuple(std::move(_r)), std::move(*_res)));
        }

        void __stop()noexcept{
            if(_ctx){
                ASIO::post(_ctx->get_executor().get_executor(), __completion_task_t<STDEXEC::set_stopped_t>{
                    .self{this},
                    .allocator{&_buf}
                });
                return;
            }
            STDEXEC::set_stopped(std::move(_r));
        }

        void __error()noexcept{
            if(_ctx){
                ASIO::post(_ctx->get_executor().get_executor(), __completion_task_t<STDEXEC::set_error_t>{
                    .self{this},
                    .allocator{&_buf}
                });
                return;
            }
            STDEXEC::set_error(std::move(_r), std::move(_err));
        }

        void __init(){
            this->_init(use_sender_handler_base<Args...>{
                .op{this},
                .allocator{&this->_buf}
            });
        }

        void complete(Args ...args)noexcept override{
            _res.emplace(std::move(args)...);
            using __first_t = asio2exec::__detail::__unwrap_first_t<decltype(*_res)>;
            if constexpr(std::is_convertible_v<__first_t, std::error_code>){
                if(asio2exec::__detail::__unwrap_first(*_res) == std::errc::operation_canceled){
                    __stop();
                    return;
                }
            }
            __value();
        }
    };

    template<STDEXEC::receiver R>
    struct __operation: __operation_base<R> {
        __operation(__initializer<Args...>&& i, R&& r, asio2exec::asio_context *ctx = nullptr)
            : __operation_base<R>(std::move(i), std::move(r), ctx)
        {}

        enum struct __state_t: char{
            construction, emplaced, initiated, stopped
        };

        ASIO::cancellation_signal _signal{};
        std::atomic<__state_t> _state{__state_t::construction};

        struct __stop_t{
            __operation *self;
            void operator()()noexcept{
                __state_t expected = self->_state.load(std::memory_order_relaxed);
                while(!self->_state.compare_exchange_weak(expected, __state_t::stopped, std::memory_order_acq_rel))
                {}
                if(expected == __state_t::initiated){
                    self->_signal.emit(ASIO::cancellation_type_t::total);
                }
            }
        };

        using __stop_callback_t = typename STDEXEC::stop_token_of_t<STDEXEC::env_of_t<R>&>:: template callback_type<__stop_t>;
        std::optional<__stop_callback_t> _stop_callback{};

        void __init(){
            this->_init(use_sender_handler<Args...>{
                {
                    .op{this},
                    .allocator{&this->_buf}
                },
                _signal.slot()
            });
        }

        void complete(Args ...args)noexcept override{
            _stop_callback.reset();
            __operation_base<R>::complete(std::move(args)...);
        }

        friend void tag_invoke(STDEXEC::start_t, __operation& self)noexcept
        {
            const auto st = STDEXEC::get_stop_token(STDEXEC::get_env(self._r));
            if(st.stop_requested()){
                self.__stop();
                return;
            }
            self._stop_callback.emplace(st, __stop_t{&self});
            __state_t expected = __state_t::construction;
            if(!self._state.compare_exchange_strong(expected, __state_t::emplaced, std::memory_order_acq_rel)){
                self._stop_callback.reset();
                self.__stop();
                return;
            }
            //初始化IO
            try{
                self.__init();
            }catch(...){
                self._stop_callback.reset();
                self._err = std::current_exception();
                self.__error();
                return;
            }
            // 如果没有请求取消，self._state == __state_t::emplaced
            expected = __state_t::emplaced;
            if(!self._state.compare_exchange_strong(expected, __state_t::initiated, std::memory_order_acq_rel)){
                // 已经请求取消，但stop_callback不会发出取消信号（见__stop_t的if分支）
                self._stop_callback.reset();
                self._signal.emit(ASIO::cancellation_type_t::total);
                return;
            }
        }
    };

    struct __transfer_sender {
        using sender_concept = STDEXEC::sender_t;

        using completion_signatures = STDEXEC::completion_signatures<
            STDEXEC::set_value_t(Args...),
            STDEXEC::set_error_t(std::exception_ptr),
            STDEXEC::set_stopped_t()
        >;

        __initializer<Args...> _init;

        template<STDEXEC::receiver R>
        struct __transfer_op_without_cancellation: __operation_base<R> {
            __transfer_op_without_cancellation(__initializer<Args...>&& i, R&& r, asio2exec::asio_context *ctx = nullptr)
                : __operation_base<R>(std::move(i), std::move(r), ctx)
            {}

            friend void tag_invoke(STDEXEC::start_t, __transfer_op_without_cancellation& self)noexcept
            {
                try {
                    self.__init();
                }
                catch (...) {
                    self._err = std::current_exception();
                    self.__error();
                }
            }
        };

        template<STDEXEC::receiver R>
        friend STDEXEC::operation_state auto tag_invoke(STDEXEC::connect_t, __transfer_sender&& self, R&& r){
            if constexpr(STDEXEC::unstoppable_token<STDEXEC::stop_token_of_t<STDEXEC::env_of_t<R>>>){
                return __transfer_op_without_cancellation<R>(
                    std::move(self._init),
                    std::forward<R>(r)
                );
            }else{
                return __operation<R>(
                    std::move(self._init),
                    std::forward<R>(r)
                );
            }
        }
    };

    template<STDEXEC::receiver R>
    struct __asio_op_without_cancellation: __operation_base<R> {
        __asio_op_without_cancellation(__initializer<Args...>&& i, R&& r, asio2exec::asio_context *ctx = nullptr)
            : __operation_base<R>(std::move(i), std::move(r), ctx)
        {}
        
        friend void tag_invoke(STDEXEC::start_t, __asio_op_without_cancellation& self)noexcept{
            try {
                self.__init();
            }
            catch (...) {
                self._err = std::current_exception();
                self.__error();
            }
        }
    };

    template<STDEXEC::receiver R>
    friend STDEXEC::operation_state auto tag_invoke(STDEXEC::connect_t, __sender&& self, R&& r)
    {
        const auto env = STDEXEC::get_env(r);
        auto sched = STDEXEC::get_scheduler(env);

        if constexpr(std::is_same_v<std::decay_t<decltype(sched)>, asio2exec::asio_context::scheduler_t>){
            const ASIO::any_io_executor ex = sched._ctx->get_executor().get_executor();
            const ASIO::any_io_executor& associated_executor = ASIO::get_associated_executor(self._init);
            if constexpr(STDEXEC::unstoppable_token<STDEXEC::stop_token_of_t<STDEXEC::env_of_t<R>>>){
                return __asio_op_without_cancellation<R>(
                    std::move(self._init),
                    std::forward<R>(r),
                    ex == associated_executor ? nullptr : sched._ctx
                );
            }else{
                return __operation<R>(
                    std::move(self._init),
                    std::forward<R>(r),
                    ex == associated_executor ? nullptr : sched._ctx
                );
            }
        }else {
            return STDEXEC::connect(
                STDEXEC::transfer(
                    __transfer_sender{
                        ._init{std::move(self._init)},
                    }, 
                    sched
                ), 
                std::forward<R>(r)
            );
        }
    }

    using completion_signatures = STDEXEC::completion_signatures<
        STDEXEC::set_value_t(Args...),
        STDEXEC::set_error_t(std::exception_ptr),
        STDEXEC::set_stopped_t()
    >;
}; // __sender

}// __detail

__detail::scheduler_t asio_context::get_scheduler()noexcept { return __detail::scheduler_t{ this }; }

}// asio2exec

namespace ASIO {
    template<class ...Args>
    struct async_result<asio2exec::use_sender_t, void(Args...)> {
        using return_type = asio2exec::__detail::__sender<Args...>;

        template<class Initiation, class ...InitArgs>
        static STDEXEC::sender auto initiate(
            Initiation&& init,
            asio2exec::use_sender_t,
            InitArgs&& ...args
        ){
            return return_type{
                asio2exec::__detail::__initializer<Args...>(
                    std::forward<Initiation>(init),
                    std::forward<InitArgs>(args)...
                )
            };
        }

    };
} // ASIO
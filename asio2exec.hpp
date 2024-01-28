#pragma once

#include "asio/async_result.hpp"
#include "asio/error_code.hpp"
#include "asio/io_context.hpp"
#include "asio/cancellation_signal.hpp"
#include "asio/associated_executor.hpp"
#include "asio/post.hpp"
#include "stdexec/execution.hpp"
#include <thread>
#include <type_traits>
#include "asio/bind_executor.hpp"
#include "asio/bind_allocator.hpp"
#include <memory_resource>
#include <cassert>
#include <format>
#include <optional>
#include <atomic>

#define STDEXEC stdexec
#define ASIO asio

namespace asio2exec {

namespace __detail{ struct scheduler_t; }

class asio_context {
    friend struct __detail::scheduler_t;
public:
    using scheduler_t = __detail::scheduler_t;
    
    explicit asio_context(int hint = -1) :
        _ctx{ hint },
        _guard{ ASIO::make_work_guard(_ctx) } {}

    asio_context(const asio_context&) = delete;
    asio_context(asio_context&&) = delete;
    asio_context& operator=(const asio_context&) = delete;
    asio_context& operator=(asio_context&&) = delete;

    ~asio_context() {
        stop();
    }

    void start() {
        _th = std::jthread([this] {
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
    std::jthread _th;
};

struct use_sender_t {
// template<class InnerExecutor>
// struct executor_with_default : InnerExecutor {
//     using default_completion_token_type = use_sender_t;
//     executor_with_default(const InnerExecutor& ex)noexcept : InnerExecutor(ex) {}
//     template<class _InnerExecutor>
//         requires !std::is_same_v<_InnerExecutor, executor_with_default> &&
//                 std::is_convertible<_InnerExecutor, InnerExecutor>
//     executor_with_default(const _InnerExecutor& ex)noexcept : InnerExecutor(ex) {}
// };
// template<class T>
// using as_default_on_t = typename T::template rebind_executor<
//     executor_with_default<typename T::executor_type>
// >::other;
// template<class T>
// static typename std::decay_t<T>::template rebind_executor<
//     executor_with_default<typename std::decay_t<T>::executor_type>
// >::other
//     as_default_on(T&& obj) 
// {
//     return typename std::decay_t<T>::template rebind_executor<
//         executor_with_default<typename std::decay_t<T>::executor_type>
//     >::other(std::forward<T>(obj));
// }
// };
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
        // std::cout << std::format("Allocate:{} bytes, Alignment:{}, buffer size:{}, buffer alignment:{}\n", bytes, alignment, sizeof(_storage), alignof(T));
        if(_used || bytes > Size || alignment > Alignment){
            // std::cout << "Can not optimize $$$$$$$$$$$$$$$$$$$$$$$$$$$\n";
            // std::terminate();
            assert(("Upstream memory_resource is empty.", _upstream));
            return _upstream->allocate(bytes, alignment);
        }
        // std::cout << "Optimize ########################################\n";
        _used = true;
        return &_storage;
    }

    void do_deallocate(void* ptr, size_t bytes, size_t alignment) override {
        // std::cout << "My deallocate.\n";
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
        using is_sender = __schedule_sender_t;
        asio_context* _ctx;

        using __completion_signatures = STDEXEC::completion_signatures<
            STDEXEC::set_value_t(),
            STDEXEC::set_error_t(std::exception_ptr),
            STDEXEC::set_stopped_t()
        >;

        template<class Env>
        friend auto tag_invoke(STDEXEC::get_completion_signatures_t, const __schedule_sender_t&, const Env&)noexcept
            -> __completion_signatures{ 
            return {}; 
        }

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
                        // std::cout << "Stop before scheduled.\n";
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
    
template<class T>
struct use_sender_handler_base {
    // using executor_type = asio::io_context::executor_type;
    using allocator_type = std::pmr::polymorphic_allocator<>;

    T* op;
    allocator_type allocator;

    allocator_type get_allocator() const noexcept { return allocator; }

    // const executor_type& get_executor() const noexcept { return _ex; }

    template<class ..._Args>
    void operator()(_Args&& ...args) {
        op->template complete(std::forward<_Args>(args)...);
    }
};

template<class T>
struct use_sender_handler: use_sender_handler_base<T> {
    using cancellation_slot_type = ASIO::cancellation_slot;
    cancellation_slot_type slot;
    cancellation_slot_type get_cancellation_slot() const noexcept { return slot; }
};

template<class ...Args>
consteval size_t __get_args_size(){
    if constexpr((sizeof ...(Args)) == 0)
        return 0ull;
    else return (sizeof(Args) + ...);
}

}// __detail

__detail::scheduler_t asio_context::get_scheduler()noexcept { return __detail::scheduler_t{ this }; }

}// asio2exec

namespace ASIO {

    template<class ...Args>
    struct async_result<asio2exec::use_sender_t, void(Args...)> {
        template<class Initiation, class ...InitArgs>
        struct __sender {
            using is_sender = __sender;

            Initiation initiation;
            std::tuple<InitArgs...> args;

            template<class Init, STDEXEC::receiver R>
            struct __operation_base {
                using __res_t = std::conditional_t<sizeof...(Args) == 0, bool, std::optional<std::tuple<Args...>>>;
                Init init;
                std::tuple<InitArgs...> args;
                R _r;
                asio2exec::asio_context *_ctx{nullptr};
                __res_t _res{};
                std::exception_ptr _err{};

                __operation_base(Init&& i, std::tuple<InitArgs...>&& a, R&& r, asio2exec::asio_context *ctx = nullptr)
                    : init{std::move(i)}, args{std::move(a)}, _r{std::move(r)}, _ctx{ctx}
                {}

                __operation_base(const __operation_base&) = delete;
                __operation_base(__operation_base&&) = delete;
                __operation_base& operator=(const __operation_base&) = delete;
                __operation_base& operator=(__operation_base&&) = delete;

                template<class Tag>
                struct __completion_task_t {
                    __operation_base *self;
                    using allocator_type = std::pmr::polymorphic_allocator<>;
                    allocator_type allocator;

                    allocator_type get_allocator() const noexcept { return allocator; }

                    void operator()()noexcept{
                        if constexpr(std::is_same_v<Tag, STDEXEC::set_value_t>){
                            if constexpr(std::is_same_v<__operation_base::__res_t, bool>){
                                STDEXEC::set_value(std::move(self->_r));
                            }else
                                std::apply(STDEXEC::set_value, std::tuple_cat(std::make_tuple(std::move(self->_r)), std::move(*self->_res)));
                        }else if constexpr(std::is_same_v<Tag, STDEXEC::set_error_t>){
                            STDEXEC::set_error(std::move(self->_r), std::move(self->_err));
                        }else if constexpr(std::is_same_v<Tag, STDEXEC::set_stopped_t>){
                            STDEXEC::set_stopped(std::move(self->_r));
                        }else{
                            static_assert(false, "Tag should one of set_value_t, set_error_t or set_stopped_t.");
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
                    if constexpr(std::is_same_v<__res_t, bool>){
                        STDEXEC::set_value(std::move(_r));
                    }else
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

                // template<class T>
                //     requires std::is_base_of_v<__operation_base, T>
                void __init()noexcept{
                    std::apply(
                        [this](InitArgs&& ...init_args) {
                            std::move(this->init)(
                                asio2exec::__detail::use_sender_handler_base<__operation_base>{
                                    .op{this},
                                    .allocator{&this->_buf}
                                },
                                std::move(init_args)...
                            );
                        },
                        std::move(this->args)
                    );
                }

                template<class ..._Args>
                void complete(_Args&& ...args)noexcept {
                    if constexpr(!std::is_same_v<__res_t, bool>){
                        _res.emplace(std::forward<_Args>(args)...);
                        if constexpr(std::is_same_v<std::decay_t<decltype(std::get<0>(*_res))>, asio::error_code>){
                            if(std::get<0>(*_res) == ASIO::error::operation_aborted){
                                __stop();
                                return;
                            }
                        }
                    }
                    __value();
                }

            };

            template<class Init, STDEXEC::receiver R>
            struct __operation: __operation_base<Init, R> {
                __operation(Init&& i, std::tuple<InitArgs...>&& a, R&& r, asio2exec::asio_context *ctx = nullptr)
                    : __operation_base<Init, R>(std::move(i), std::move(a), std::move(r), ctx)
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
                        // std::cout << "$$$$$$$$$$$$$$$$:" << size_t(self) << '\n';
                        if(expected == __state_t::initiated){
                            self->_signal.emit(ASIO::cancellation_type_t::total);
                        }
                    }
                };

                using __stop_callback_t = typename STDEXEC::stop_token_of_t<STDEXEC::env_of_t<R>&>:: template callback_type<__stop_t>;

                std::optional<__stop_callback_t> _stop_callback{};

                void __init()noexcept{
                    std::apply(
                        [this](InitArgs&& ...init_args) {
                            std::move(this->init)(
                                asio2exec::__detail::use_sender_handler<__operation>{
                                    {
                                        .op{this},
                                        .allocator{&this->_buf}
                                    },
                                    _signal.slot()
                                },
                                std::move(init_args)...
                            );
                        },
                        std::move(this->args)
                    );
                }

                template<class ..._Args>
                void complete(_Args&& ...args)noexcept {
                    _stop_callback.reset();
                    __operation_base<Init, R>::complete(std::forward<_Args>(args)...);
                }

                friend void tag_invoke(STDEXEC::start_t, __operation& self)noexcept
                {
                    const auto st = STDEXEC::get_stop_token(STDEXEC::get_env(self._r));
                    if(st.stop_requested()){
                        // std::cout << "Stop1.\n";
                        self.__stop();
                        return;
                    }
                    self._stop_callback.emplace(st, __stop_t{&self});
                    __state_t expected = __state_t::construction;
                    if(!self._state.compare_exchange_strong(expected, __state_t::emplaced, std::memory_order_acq_rel)){
                        // 初始化IO操作前已经请求取消, stop_callback已调用
                        // std::cout << "Stop2.\n";
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
                        // std::cout << "Stop3.\n";
                        self._stop_callback.reset();
                        self._signal.emit(ASIO::cancellation_type_t::total);
                        return;
                    }
                    // std::cout << "Initiate normally.\n";
                }
            };

            struct __transfer_sender {
                using is_sender = __transfer_sender;

                using __completion_signatures = STDEXEC::completion_signatures<
                    STDEXEC::set_value_t(Args...),
                    STDEXEC::set_error_t(std::exception_ptr),
                    STDEXEC::set_stopped_t()
                >;

                template<class Env>
                friend auto tag_invoke(STDEXEC::get_completion_signatures_t, const __transfer_sender&, Env)noexcept
                    -> __completion_signatures{ 
                    return {}; 
                }

                Initiation init;
                std::tuple<InitArgs...> args;

                template<class Init, STDEXEC::receiver R>
                struct __transfer_op_without_cancellation: __operation_base<Init, R> {
                    __transfer_op_without_cancellation(Init&& i, std::tuple<InitArgs...>&& a, R&& r, asio2exec::asio_context *ctx = nullptr)
                        : __operation_base<Init, R>(std::move(i), std::move(a), std::move(r), ctx)
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
                        return __transfer_op_without_cancellation<std::decay_t<Initiation>, R>(
                            std::move(self.init),
                            std::move(self.args),
                            std::forward<R>(r)
                        );
                    }else{
                        return __operation<std::decay_t<Initiation>, R>(
                            std::move(self.init),
                            std::move(self.args),
                            std::forward<R>(r)
                        );
                    }
                }
            };

            template<class Init, STDEXEC::receiver R>
            struct __asio_op_without_cancellation: __operation_base<Init, R> {
                __asio_op_without_cancellation(Init&& i, std::tuple<InitArgs...>&& a, R&& r, asio2exec::asio_context *ctx = nullptr)
                    : __operation_base<Init, R>(std::move(i), std::move(a), std::move(r), ctx)
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
                const auto sched = STDEXEC::get_scheduler(env);

                if constexpr(std::is_same_v<std::decay_t<decltype(sched)>, asio2exec::asio_context::scheduler_t>){
                    const auto& ex = sched._ctx->get_executor().get_executor();
                    const auto& associated_executor = get_associated_executor(self.initiation);
                    if constexpr(STDEXEC::unstoppable_token<STDEXEC::stop_token_of_t<STDEXEC::env_of_t<R>>>){
                        return __asio_op_without_cancellation<std::decay_t<Initiation>, R>(
                            std::move(self.initiation),
                            std::move(self.args),
                            std::forward<R>(r),
                            ex == associated_executor ? nullptr : sched._ctx
                        );
                    }else{
                        return __operation<std::decay_t<Initiation>, R>(
                            std::move(self.initiation),
                            std::move(self.args),
                            std::forward<R>(r),
                            ex == associated_executor ? nullptr : sched._ctx
                        );
                    }
                }else {
                    return STDEXEC::connect(
                        STDEXEC::transfer(
                            __transfer_sender{
                                .init{std::move(self.initiation)},
                                .args{std::move(self.args)}
                            }, 
                            sched
                        ), 
                        std::forward<R>(r)
                    );
                }
            }

            using __completion_signatures = STDEXEC::completion_signatures<
                STDEXEC::set_value_t(Args...),
                STDEXEC::set_error_t(std::exception_ptr),
                STDEXEC::set_stopped_t()
            >;

            template<class Env>
            friend auto tag_invoke(STDEXEC::get_completion_signatures_t, const __sender&, Env)noexcept
                -> __completion_signatures{ 
                return {}; 
            }
        };

        template<class Initiation, class ...InitArgs>
        static STDEXEC::sender auto initiate(
            Initiation&& init,
            asio2exec::use_sender_t,
            InitArgs&& ...args
        ){
            return __sender<std::decay_t<Initiation>, std::decay_t<InitArgs>...>{
                .initiation{std::move(init)},
                .args{std::move(args)...} 
            };
        }
    };

} // ASIO
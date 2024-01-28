#include "asio/io_context.hpp"
#include "asio/steady_timer.hpp"
#include "asio/buffer.hpp"
#include "asio/ip/tcp.hpp"
#include "asio/write.hpp"
#include "asio2exec.hpp"


#include "stdexec/execution.hpp"
#include "exec/task.hpp"
#include "exec/when_any.hpp"
#include "exec/repeat_effect_until.hpp"
#include <thread>
#include <iostream>
#include <format>
#include <cstdio>
#include <array>
#include <span> 
// 


namespace ex = stdexec;
using namespace asio2exec;


void test_sync_wait(){
    stdexec::sender auto work = stdexec::just() | stdexec::then([ ] { std::cout << "Fuck you!\n"; });
    stdexec::sync_wait(std::move(work));
}

void
test_use_sender() {
    asio::io_context ctx;
    auto guard = asio::make_work_guard(ctx);
    std::jthread th{ [&] { ctx.run(); } };

    asio::steady_timer timer{ ctx, std::chrono::seconds(5) };
    stdexec::sender auto work = stdexec::just() |
                                stdexec::let_value([&] {
                                    std::cout << "let_value running on " << std::this_thread::get_id() << '\n';
                                    return timer.async_wait(use_sender);
                                }) |
                                stdexec::then([ ](asio::error_code err) {
                                    std::cout << "then() running on " << std::this_thread::get_id() << '\n';
                                    if (err)
                                        std::cerr << "Error.\n";
                                    else std::cout << "Fuck you man!\n";
                                });
    std::cout << "main() running on " << std::this_thread::get_id() << '\n';
    stdexec::sync_wait(std::move(work));
    guard.reset();
}

void test_resolve(){
    asio::io_context ctx;
    auto guard = asio::make_work_guard(ctx);
    std::jthread th{ [&] { ctx.run(); } };

    asio::ip::tcp::resolver resolver{ ctx };
    stdexec::sender auto work = stdexec::just() |
                                stdexec::let_value([&] {
                                    std::cout << "let_value running on " << std::this_thread::get_id() << '\n';
                                    return resolver.async_resolve("pytorch.org", "https", use_sender);
                                }) |
                                stdexec::then([ ](asio::error_code err, auto entries) {
                                    std::cout << "then() running on " << std::this_thread::get_id() << '\n';
                                    if (err)
                                        std::cerr << "Error.\n";
                                    else for(const auto& entry: entries){
                                        std::cout << entry.host_name() << '\n';
                                    }
                                });
    std::cout << "main() running on " << std::this_thread::get_id() << '\n';
    stdexec::sync_wait(std::move(work));
    guard.reset();
}

void test_asio_context() {
    asio_context ctx;
    ctx.start();
    auto sched = ctx.get_scheduler();
    asio::steady_timer timer{ ctx.get_executor(), std::chrono::seconds(3) };
    asio::ip::tcp::resolver resolver{ ctx.get_executor() };

    auto work = ex::schedule(sched) |
                ex::let_value([&] {
                    return timer.async_wait(use_sender);
                }) |
                ex::then([ ](asio::error_code e) {
                    if (e) {
                        std::cerr << "Error.\n";
                    }
                    else {
                        std::cout << "Fuck you, man!\n";
                    }
                }) |
                ex::let_value([&] {
                    return resolver.async_resolve("pytorch.org", "https", use_sender);
                }) |  
                ex::then([ ](asio::error_code err, auto entries) {
                    if (err)
                        std::cerr << "Error.\n";
                    else for(const auto& entry: entries){
                        std::cout << entry.host_name() << '\n';
                    }
                });

    ex::sync_wait(std::move(work));
}

void test_context_transfer(){
    asio_context ctx1;
    asio_context ctx2;

    ex::scheduler auto sched1 = ctx1.get_scheduler();
    ex::scheduler auto sched2 = ctx2.get_scheduler();

    ctx1.start();
    ctx2.start();

    {
        std::cout << "No scheduler.\n";
        asio::steady_timer timer{ ctx1.get_executor(), std::chrono::seconds(2) };
        std::cout << "Main thread is: " << std::this_thread::get_id() << '\n';
        ex::sender auto work = timer.async_wait(use_sender) | 
                                ex::then([](asio::error_code e){
                                    std::cout << "Down stream running on " << std::this_thread::get_id() << '\n';
                                    if(e)
                                        std::cout << "Error.\n";
                                    else std::cout << "Fuck you, man!\n";
                                });
        ex::sync_wait(std::move(work));
    }

    {
        std::cout << "\nUse asio_context.\n";

        asio::io_context empty_ctx;
        asio::steady_timer timer{ ctx2.get_executor(), std::chrono::seconds(1) };

        ex::sender auto work = ex::schedule(sched1) |
                                ex::then([]{ std::cout << "ctx1 is: " << std::this_thread::get_id() << '\n'; }) |
                                ex::transfer(sched2) |
                                ex::then([]{ std::cout << "ctx2 is: " << std::this_thread::get_id() << '\n'; }) |
                                ex::transfer(sched1) |
                                ex::then([]{ std::cout << "Come back: " << std::this_thread::get_id() << '\n'; }) |
                                ex::let_value([&]{
                                    return timer.async_wait(use_sender);
                                }) |
                                ex::then([](asio::error_code err){
                                    std::cout << "Down stream running on " << std::this_thread::get_id() << '\n';
                                    if (err)
                                        std::cerr << "Error.\n";
                                    else std::cout << "Fuck you again!\n";
                                });
        ex::sync_wait(std::move(work));
    }

    {
        std::cout << "\nUse stdexec::on.\n";

        asio::steady_timer timer{ ctx2.get_executor(), std::chrono::seconds(1) };

        ex::sender auto work =  ex::just() |
                                ex::then([]{ std::cout << "ctx1 is: " << std::this_thread::get_id() << '\n'; }) |
                                ex::transfer(sched2) |
                                ex::then([]{ std::cout << "ctx2 is: " << std::this_thread::get_id() << '\n'; }) |
                                ex::transfer(sched1) |
                                ex::then([]{ std::cout << "Come back: " << std::this_thread::get_id() << '\n'; }) |
                                ex::let_value([&]{
                                    return timer.async_wait(use_sender);
                                }) |
                                ex::then([](asio::error_code err){
                                    std::cout << "Down stream running on " << std::this_thread::get_id() << '\n';
                                    if (err)
                                        std::cerr << "Error.\n";
                                    else std::cout << "Fuck you again!\n";
                                });
        ex::sync_wait(ex::on(sched1, std::move(work)));
    }

    {
        std::cout << "\nCoroutine.\n";
        asio::steady_timer timer{ ctx2.get_executor(), std::chrono::seconds(1) };

        ex::sender auto work =  ex::just() |
                                ex::then([]{ std::cout << "Coroutine thread is: " << std::this_thread::get_id() << '\n'; }) |
                                ex::transfer(sched2) |
                                ex::then([]{ std::cout << "ctx2 is: " << std::this_thread::get_id() << '\n'; }) |
                                ex::transfer(sched1) |
                                ex::then([]{ std::cout << "ctx1 is: " << std::this_thread::get_id() << '\n'; }) |
                                ex::let_value([&]{
                                    return timer.async_wait(use_sender);
                                }) |
                                ex::then([](asio::error_code err){
                                    std::cout << "Down stream running on " << std::this_thread::get_id() << '\n';
                                    if (err)
                                        std::cerr << "Error.\n";
                                    else std::cout << "Fuck you again!\n";
                                });
        auto coro = [](ex::sender auto work)->exec::task<void>{
            std::cout << "Coroutine is running on " << std::this_thread::get_id() << '\n';
            ex::scheduler auto sched = co_await ex::get_scheduler();
            co_await ex::on(sched, std::move(work));
            std::cout << "After co_await, coroutine is running on " << std::this_thread::get_id() << '\n';
        };

        ex::sync_wait(coro(std::move(work)));
    }
}

template<class ...Args>
consteval size_t __get_args_size(){
    if constexpr((sizeof ...(Args)) == 0)
        return 0ull;
    else return (sizeof(Args) + ...);
}

void test_when_all() {
    asio_context ctx;
    ctx.start();

    asio::steady_timer timer1{ctx.get_executor(), std::chrono::seconds(100)};
    asio::steady_timer timer2{ctx.get_executor(), std::chrono::seconds(5)};

    // std::cout << "Fuck you!\n";
    // ex::sync_wait(ex::when_all(
    //     timer1.async_wait(use_sender),
    //     timer2.async_wait(use_sender)
    // ));
    // std::cout << "Fuck you again!\n";

    std::cout << "\nTesting cancel...\n";
    // timer1.expires_after(std::chrono::seconds(60));
    // timer2.expires_after(std::chrono::seconds(3));
    try{
        std::cout << "Fuck you!\n";
        ex::sync_wait(ex::when_all(
            timer1.async_wait(use_sender),
            timer2.async_wait(use_sender)
        ));
        std::cout << "Fuck you again" << '\n';
    }catch(const std::exception& e){
        std::cout << "Timer had been canceled. Throw:" << e.what() << '\n';
    }
}

template<size_t Size>
struct inplace_buffer{
    inplace_buffer()noexcept = default;

    inplace_buffer(const inplace_buffer&) = delete;
    inplace_buffer(inplace_buffer&&) = delete;
    inplace_buffer& operator=(const inplace_buffer&) = delete;
    inplace_buffer& operator=(inplace_buffer&&) = delete;

    std::array<char, Size> buf;
};

// void test_echo_server() {
//     asio_context ctx;
//     ctx.start();

//     auto sched = ctx.get_scheduler();
//     asio::ip::tcp::acceptor acceptor{ ctx.get_executor(), asio::ip::tcp::endpoint(asio::ip::make_address_v4("0.0.0.0"), 8080) };
    
//     auto work = ex::schedule(sched) |
//                 ex::let_value([&]{
//                     return acceptor.async_accept(use_sender);
//                 }) |
//                 ex::then([&](auto err, asio::ip::tcp::socket s){
//                     if(err)
//                         throw std::runtime_error{"Accept error."};
//                     auto echo_work =    ex::just(std::array<char, 1024>{}, std::move(s)) |
//                                         ex::let_value([&](std::array<char, 1024>& buf, asio::ip::tcp::socket& s){
//                                             return  ex::just() |
//                                                     ex::let_value([&]{
//                                                         return s.async_read_some(asio::buffer(buf.data(), buf.size()), use_sender);
//                                                     }) |
//                                                     ex::then([&](auto err, size_t n){
//                                                         if(err || n == 0)
//                                                             throw std::runtime_error{"Read error."};
//                                                         return std::span<char>{buf.data(), n};
//                                                     }) |
//                                                     ex::let_value([&](const std::span<char> data){
//                                                         return asio::async_write(s, asio::buffer(data.data(), data.size()), use_sender);
//                                                     }) |
//                                                     // ex::upon_error([](auto){ return true; }) |
//                                                     ex::then([&](auto...){ return !s.is_open(); }) |
//                                                     exec::repeat_effect_until();
//                                         });
//                     ex::start_detached(ex::on(sched, std::move(echo_work)));
//                 }) |
//                 // ex::upon_error([](auto){ return true; }) |
//                 ex::then([&]{ return !acceptor.is_open(); }) |
//                 exec::repeat_effect_until();

//     ex::sync_wait(std::move(work));
// }

void test_echo_server() {
    asio_context ctx;
    ctx.start();

    ex::scheduler auto sched = ctx.get_scheduler();
    asio::ip::tcp::acceptor acceptor{ ctx.get_executor(), asio::ip::tcp::endpoint(asio::ip::make_address_v4("0.0.0.0"), 8080) };

    auto work = ex::schedule(sched) |
                ex::let_value([&]{
                    return acceptor.async_accept(use_sender) |
                            ex::then([](auto err, asio::ip::tcp::socket socket){
                                if(err)
                                    throw std::runtime_error{"Accept error."};
                                return socket;
                            });
                }) |
                ex::then([&](asio::ip::tcp::socket socket){
                    auto echo_work = ex::just(std::move(socket), std::array<char, 1024>{}, asio::steady_timer{ctx.get_executor()}) |
                                    ex::let_value([](asio::ip::tcp::socket& s, std::array<char, 1024>& buf, asio::steady_timer& timer){
                                        timer.expires_after(std::chrono::seconds(180));
                                        return  exec::when_any(
                                                    s.async_read_some(asio::buffer(buf.data(), buf.size()), use_sender),
                                                    timer.async_wait(use_sender)
                                                ) |
                                                ex::then([](auto err, size_t n){
                                                    if(err)
                                                        throw std::runtime_error{"Read error."};
                                                    return n;
                                                }) |
                                                ex::let_value([&](size_t n){
                                                    std::string_view msg{buf.data(), n};
                                                    std::cout << msg << '\n';
                                                    return asio::async_write(s, asio::buffer(buf.data(), n), use_sender) |
                                                            ex::then([&](auto err, auto){
                                                                if(err)
                                                                    throw std::runtime_error{"Write error."};
                                                                return !s.is_open();
                                                            });
                                                }) |
                                                ex::upon_error([](auto){
                                                    std::cerr << "Error or disconnected.\n";
                                                    return true;
                                                }) |
                                                exec::repeat_effect_until();
                                    });

                    ex::start_detached(ex::on(sched, std::move(echo_work)));
                    return !acceptor.is_open();
                }) |
                ex::upon_error([](auto){
                    std::cerr << "Accept error, exit.\n";
                    return true;
                }) |
                exec::repeat_effect_until();
                // ex::let_value([](asio::ip::tcp::socket socket){
                    // return  ex::just(/*std::make_tuple(std::move(socket), std::array<char, 1024>{})*/);
                            // ex::let_value([](auto args){
                            //     std::cout << "Type:" << typeid(args).name() << '\n';
                            //     // auto& socket = std::get<0>(args);
                            //     // auto& buf = std::get<1>(args);
                            //     return ex::just();
                            // });
                // }) |
                // ex::then([&](){ return !acceptor.is_open(); });
                // exec::repeat_effect_until();

    ex::sync_wait(std::move(work));
    // std::cout << res << '\n';
}

int main() {
    // test_hello();
    // test_use_sender();
    // test_sync_wait();
    // test_resolve();
    // test_asio_context();
    // test_context_transfer();
    // test_when_all();
    test_echo_server();
}

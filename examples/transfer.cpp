#include "stdexec/execution.hpp"
#include "asio2exec.hpp"
#include "asio/steady_timer.hpp"
#include "asio/post.hpp"
#include <iostream>

namespace ex = stdexec;

int main(){
    std::cout << "Main thread: " << std::this_thread::get_id() << std::endl;

    asio2exec::asio_context ctx;
    ctx.start();

    asio::post(ctx.get_executor(), [](){
        std::cout << "IO context thread: " << std::this_thread::get_id() << std::endl;
    });

    asio::steady_timer timer{ctx.get_executor(), std::chrono::seconds(1)};

    auto work = timer.async_wait(asio2exec::use_sender) |
                ex::then([](asio::error_code ec){
                    std::cout << "Running in main thread:" << std::this_thread::get_id() << std::endl;
                }) |
                ex::continues_on(ctx.get_scheduler()) |
                ex::then([]{
                    std::cout << "Running in io context thread:" << std::this_thread::get_id() << std::endl;
                });

    ex::sync_wait(std::move(work));
}
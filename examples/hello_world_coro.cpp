#include <stdexec/execution.hpp>
#include <exec/task.hpp>
#include <exec/start_detached.hpp>
#include <asio/steady_timer.hpp>

#include "asio2exec.hpp"

#include <iostream>

namespace ex = stdexec;
using namespace asio2exec;

exec::task<std::string> hello(asio::io_context& ctx){
    asio::steady_timer timer{ctx, std::chrono::seconds(3)};
    asio::error_code ec = co_await timer.async_wait(use_sender);
    if(ec)
        throw asio::system_error(ec);
    co_return "Hello World";
}

int main(){
    asio::io_context ctx;
    exec::start_detached(ex::starts_on(asio2exec::scheduler{ctx}, hello(ctx) | ex::then([](std::string str) { std::cout << str << '\n'; })));
    ctx.run();
}
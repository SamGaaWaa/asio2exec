#include "stdexec/execution.hpp"
#include "exec/task.hpp"
#include "asio2exec.hpp"
#include "asio/steady_timer.hpp"
#include <iostream>

namespace ex = stdexec;
using namespace asio2exec;

exec::task<std::string> hello(asio_context& ctx){
    asio::steady_timer timer{ctx.get_executor(), std::chrono::seconds(3)};
    asio::error_code ec = co_await timer.async_wait(use_sender);
    if(ec)
        throw asio::system_error{ec};
    co_return "Hello World";
}

int main(){
    asio_context ctx;
    ctx.start();

    auto [str] = ex::sync_wait(hello(ctx)).value();
    std::cout << str << '\n';
}
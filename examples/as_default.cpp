#include "stdexec/execution.hpp"
#include "asio2exec.hpp"
#include "asio/steady_timer.hpp"
#include <iostream>

namespace ex = stdexec;
using asio_timer = asio2exec::use_sender_t::as_default_on_t<asio::steady_timer>;

int main() {
    asio2exec::asio_context ctx;
    asio_timer timer{ctx.get_executor(), std::chrono::seconds(3)};

    ctx.start();

    ex::sender auto work =  timer.async_wait() |
                            ex::then([](asio::error_code ec){
                                if(ec)
                                    throw asio::system_error{ec};
                                std::cout << "Hello World\n";
                            });
    
    ex::sync_wait(std::move(work));
}

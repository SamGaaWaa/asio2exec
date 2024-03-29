#include "stdexec/execution.hpp"
#include "asio2exec.hpp"
#include "asio/steady_timer.hpp"
#include <iostream>

namespace ex = stdexec;

int main() {
    asio2exec::asio_context ctx;
    asio::steady_timer timer{ctx.get_executor(), std::chrono::seconds(3)};

    ctx.start();

    ex::sender auto work = timer.async_wait(asio2exec::use_sender) |
                            ex::then([](asio::error_code ec){
                                if(ec)
                                    throw asio::system_error{ec};
                                std::cout << "Hello World\n";
                            });
    
    ex::sync_wait(std::move(work));
}

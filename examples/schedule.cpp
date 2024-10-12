#include "stdexec/execution.hpp"
#include "asio2exec.hpp"
#include "asio/steady_timer.hpp"
#include <iostream>
#include <thread>

namespace ex = stdexec;

int main() {
    asio::io_context ctx;
    asio2exec::scheduler sched{ctx};

    asio::steady_timer timer{ctx, std::chrono::seconds(3)};

    ex::sender auto work =  timer.async_wait(asio2exec::use_sender) |
                            ex::then([](asio::error_code ec){
                                if(ec)
                                    throw asio::system_error{ec};
                                std::cout << "Hello World\n";
                            });
    
    ex::start_detached(ex::starts_on(sched, std::move(work)));
    ctx.run();
}

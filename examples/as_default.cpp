#include <stdexec/execution.hpp>
#include <exec/start_detached.hpp>
#include <asio/steady_timer.hpp>

#include "asio2exec.hpp"

#include <iostream>

namespace ex = stdexec;
using asio_timer = asio2exec::use_sender_t::as_default_on_t<asio::steady_timer>;

int main() {
    asio::io_context ctx;
    asio_timer timer{ctx, std::chrono::seconds(3)};

    ex::sender auto work =  timer.async_wait() |
                            ex::then([](asio::error_code ec){
                                if(ec)
                                    throw asio::system_error{ec};
                                std::cout << "Hello World\n";
                            });

    exec::start_detached(std::move(work));
    ctx.run();
}

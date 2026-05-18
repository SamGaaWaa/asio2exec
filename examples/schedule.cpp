#include "stdexec/execution.hpp"
#include "exec/start_detached.hpp"
#include "asio2exec.hpp"
#include <iostream>
#include <thread>

namespace ex = stdexec;

int main() {
    asio::io_context ctx;
    asio2exec::scheduler sched{ctx};

    auto work1 =  ex::just() |
                    ex::then([] {
                        std::cout << "Hello World\n";
                    });

    exec::start_detached(ex::starts_on(sched, std::move(work1)));

    auto work2 = asio::post(ctx, asio2exec::use_sender) |
                 ex::then([] {
                    std::cout << "Hello World\n";
                });
    exec::start_detached(std::move(work2));

    ctx.run();
}

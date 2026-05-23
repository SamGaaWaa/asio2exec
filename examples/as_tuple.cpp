#include <stdexec/execution.hpp>
#include <exec/when_any.hpp>
#include <exec/start_detached.hpp>
#include <asio/signal_set.hpp>
#include <asio/as_tuple.hpp>

#include "asio2exec.hpp"

#include <iostream>

namespace ex = stdexec;
using namespace asio2exec;

int main() {
    asio::io_context ctx;

    asio::signal_set signals{ctx};

    signals.add(SIGINT);
    signals.add(SIGTERM);
#if defined(SIGQUIT)
    signals.add(SIGQUIT);
#endif // defined(SIGQUIT)

    auto work = signals.async_wait(asio::as_tuple(use_sender)) |
                ex::then([](std::tuple<asio::error_code, int> sig){
                    std::cout << "\nHello World\n";
                });
    std::cout << "Type \"Ctrl + C\".\n";
    exec::start_detached(std::move(work));
    ctx.run();
}

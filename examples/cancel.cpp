#include "stdexec/execution.hpp"
#include "exec/when_any.hpp"
#include "asio2exec.hpp"
#include "asio/steady_timer.hpp"
#include "asio/signal_set.hpp"
#include <iostream>

namespace ex = stdexec;
using namespace asio2exec;

int main(){
    asio_context ctx;
    ctx.start();

    asio::steady_timer timer{ctx.get_executor(), std::chrono::seconds(1000)};
    asio::signal_set signals{ctx.get_executor()};
    
    signals.add(SIGINT);
    signals.add(SIGTERM);
#if defined(SIGQUIT)
    signals.add(SIGQUIT);
#endif // defined(SIGQUIT)

    auto work = exec::when_any(
        timer.async_wait(use_sender),
        signals.async_wait(use_sender) | ex::let_value([](auto...){ return ex::just_stopped(); })
    ) | 
    ex::upon_stopped([]{
        std::cout << "\nCanceled.\n";
    }) |
    ex::into_variant();

    std::cout << "Type \"Ctrl + C\" to cancel the timer.\n";
    ex::sync_wait(std::move(work));
}
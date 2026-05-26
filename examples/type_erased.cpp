#include <stdexec/execution.hpp>
#include <exec/start_detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/high_resolution_timer.hpp>

#include "asio2exec.hpp"

#include <iostream>

auto timeout(auto& timer) {
    timer.expires_after(std::chrono::seconds(1));
    return timer.async_wait(asio2exec::use_any_sender);
}

int main() {
    asio2exec::asio_context ctx;
    ctx.start();

    asio::steady_timer t1{ctx.context()};
    asio::high_resolution_timer t2{ctx.context()};

    asio2exec::sender<std::error_code> sndr = timeout(t1);
    stdexec::sync_wait(std::move(sndr) | stdexec::then([](std::error_code) {
        std::cout << "steady_timer expired\n";
    }));

    sndr = timeout(t2);
    stdexec::sync_wait(std::move(sndr) | stdexec::then([](std::error_code) {
        std::cout << "high_resolution_timer expired\n";
    }));
}
#include "stdexec/execution.hpp"
#include "asio2exec.hpp"
#include "asio/ip/tcp.hpp"
#include <iostream>

namespace ex = stdexec;
using namespace asio2exec;

int main(){
    asio_context ctx;
    ctx.start();

    asio::ip::tcp::acceptor acceptor{ ctx.get_executor(), asio::ip::tcp::endpoint(asio::ip::make_address_v4("0.0.0.0"), 80) };
    ex::sync_wait(ex::schedule(ctx.get_scheduler()) |
                  ex::let_value([&]{
                    return acceptor.async_accept(use_sender);
                  }) |
                  ex::let_value([](auto&, asio::ip::tcp::socket& s){
                    std::cout << s.is_open() << '\n';
                    return ex::just();
                  }));
}
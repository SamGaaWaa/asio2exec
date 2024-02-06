#include "stdexec/execution.hpp"
#include "exec/when_any.hpp"
#include "exec/task.hpp"
#include "exec/repeat_effect_until.hpp"
#include "asio2exec.hpp"
#include "asio/steady_timer.hpp"
#include "asio/ip/tcp.hpp"
#include "asio/write.hpp"
#include "asio/signal_set.hpp"
#include <iostream>

namespace ex = stdexec;
using namespace asio2exec;

exec::task<void> echo_server(asio_context& ctx, const std::string_view& ip, int port){
    ex::scheduler auto sched = ctx.get_scheduler();
    asio::ip::tcp::acceptor acceptor{ ctx.get_executor(), asio::ip::tcp::endpoint(asio::ip::make_address_v4(ip), port) };

    co_await ex::schedule(sched);

    // while(true){
    //     co_await acceptor.async_accept(use_sender);
    // }
    

}

int main(int argc, char **argv){
        if(argc < 3){
        std::cout << "Usage: echo_server <IP> <PORT>\n";
        return -1;
    }

    const std::string_view ip{argv[1]};
    const int port{std::atoi(argv[2])};

    asio_context ctx;
    ctx.start();

    ex::sync_wait(echo_server(ctx, ip, port));
}
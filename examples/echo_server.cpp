#include "stdexec/execution.hpp"
#include "exec/when_any.hpp"
#include "exec/repeat_effect_until.hpp"
#include "asio2exec.hpp"
#include "asio/steady_timer.hpp"
#include "asio/ip/tcp.hpp"
#include "asio/write.hpp"
#include "asio/signal_set.hpp"
#include <iostream>

namespace ex = stdexec;
using namespace asio2exec;

int main(int argc, char **argv){
    if(argc < 3){
        std::cout << "Usage: echo_server <IP> <PORT>\n";
        return -1;
    }

    const std::string_view ip{argv[1]};
    const int port{std::atoi(argv[2])};

    asio_context ctx;
    ctx.start();

    ex::scheduler auto sched = ctx.get_scheduler();
    asio::ip::tcp::acceptor acceptor{ ctx.get_executor(), asio::ip::tcp::endpoint(asio::ip::make_address_v4(ip), port) };

    auto work = ex::schedule(sched) |
                ex::let_value([&]{
                    return acceptor.async_accept(use_sender) |
                            ex::then([](auto ec, asio::ip::tcp::socket socket){
                                if(ec)
                                    throw asio::system_error{ec};
                                return socket;
                            });
                }) |
                ex::then([&](asio::ip::tcp::socket socket){
                    auto echo_work = ex::just(std::move(socket), std::array<char, 1024>{}, asio::steady_timer{ctx.get_executor()}) |
                                    ex::let_value([](asio::ip::tcp::socket& s, std::array<char, 1024>& buf, asio::steady_timer& timer){
                                        return  ex::just() |
                                                ex::let_value([&]{
                                                    timer.expires_after(std::chrono::seconds(15));
                                                    return  exec::when_any(
                                                                s.async_read_some(asio::buffer(buf.data(), buf.size()), use_sender),
                                                                timer.async_wait(use_sender) | ex::let_value([](auto){ return ex::just_stopped(); })
                                                            );
                                                }) |
                                                ex::then([](asio::error_code ec, size_t n){
                                                    if(ec)
                                                        throw asio::system_error{ec};
                                                    return n;
                                                }) |
                                                ex::let_value([&](size_t n){
                                                    std::string_view msg{buf.data(), n};
                                                    std::cout << msg << '\n';
                                                    timer.expires_after(std::chrono::seconds(30));
                                                    return  exec::when_any(
                                                                asio::async_write(s, asio::buffer(buf.data(), n), use_sender),
                                                                timer.async_wait(use_sender) | ex::let_value([](auto){ return ex::just_stopped(); })
                                                            ) |
                                                            ex::then([&](asio::error_code ec, size_t n){
                                                                if(n == 0)
                                                                    return true;
                                                                if(ec)
                                                                    throw asio::system_error{ec};
                                                                return !s.is_open();
                                                            });
                                                }) |
                                                ex::upon_error([](auto){
                                                    std::cerr << "Error or disconnected.\n";
                                                    return true;
                                                }) |
                                                ex::upon_stopped([]{
                                                    std::cerr << "Time out.\n";
                                                    return true;
                                                }) |
                                                exec::repeat_effect_until();
                                    });

                    ex::start_detached(ex::on(sched, std::move(echo_work)));
                    return !acceptor.is_open();
                }) |
                ex::upon_error([](auto){
                    std::cerr << "Accept error, exit.\n";
                    return true;
                }) |
                ex::upon_stopped([]{
                    std::cerr << "Exited.\n";
                    return true;
                }) |
                exec::repeat_effect_until();

                
    asio::signal_set signals{ctx.get_executor()};
    signals.add(SIGINT);
    signals.add(SIGTERM);
#if defined(SIGQUIT)
    signals.add(SIGQUIT);
#endif // defined(SIGQUIT)

    ex::sync_wait(exec::when_any(
        std::move(work),
        signals.async_wait(use_sender)
    ) | ex::into_variant());

    std::cout << "Waiting detached work...\n"; 
}
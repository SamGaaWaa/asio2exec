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

ex::sender auto stop_when(ex::sender auto snd, ex::sender auto trigger){
    return  exec::when_any(
                std::move(snd), 
                std::move(trigger)
                | ex::let_value([](auto&...){
                    return ex::just_stopped();
                })
            ) | 
            ex::then([]<class ...Args>(Args&&...results){
                constexpr size_t args_n = (sizeof ...(Args));
                if constexpr(args_n == 0){
                    return;
                }else if constexpr(args_n == 1){
                    return std::get<0>(std::make_tuple(std::forward<Args>(results)...));
                }else{
                    return std::make_tuple(std::forward<Args>(results)...);
                }
            }) |
            ex::stopped_as_optional() |
            ex::then([](auto opt){
                using __res_t = decltype(opt.value());
                if constexpr(std::is_same_v<__res_t, void>){
                    return;
                }else{
                    return std::move(opt).value();
                }
            });
}

exec::task<void> session(asio_context& ctx, asio::ip::tcp::socket s){
    asio::steady_timer timer{ctx.get_executor()};
    std::array<char, 1024> buf;

    try{
        while(s.is_open()){
            timer.expires_after(std::chrono::seconds(15));
            auto [ec, n] = co_await stop_when(
                s.async_read_some(asio::buffer(buf.data(), buf.size()), use_sender),
                timer.async_wait(use_sender)
            );
            if(n == 0){
                std::cout << "Disconnected.\n";
                co_return;
            }
            if(ec)
                throw asio::system_error{ec};
            std::string_view msg{buf.data(), n};
            std::cout << msg << '\n';
            timer.expires_after(std::chrono::seconds(30));
            std::tie(ec, n) = co_await stop_when(
                asio::async_write(s, asio::buffer(buf.data(), n), use_sender),
                timer.async_wait(use_sender)
            );
            if(n == 0){
                std::cout << "Disconnected.\n";
                co_return;
            }
            if(ec)
                throw asio::system_error{ec};
        }
    }catch(const asio::system_error& e){
        std::cerr << "Error:" << e.what() << '\n';
    }catch(const std::bad_optional_access&){
        std::cerr << "Timeout.\n";
    }
}

exec::task<void> echo_server(asio_context& ctx, std::string_view ip, int port){
    ex::scheduler auto sched = ctx.get_scheduler();
    asio::ip::tcp::acceptor acceptor{ ctx.get_executor(), asio::ip::tcp::endpoint(asio::ip::make_address_v4(ip), port) };
    asio::signal_set signals{ctx.get_executor()};
    signals.add(SIGINT);
    signals.add(SIGTERM);
#if defined(SIGQUIT)
    signals.add(SIGQUIT);
#endif // defined(SIGQUIT)

    co_await ex::schedule(sched);

    try{
        while(acceptor.is_open()){
            auto [ec, sock] = co_await stop_when(
                acceptor.async_accept(use_sender),
                signals.async_wait(use_sender)
            );
            if(ec)
                throw asio::system_error{ec};
            ex::start_detached(ex::starts_on(sched, session(ctx, std::move(sock))));
        }
    }catch(const asio::system_error& e){
        std::cerr << "Accept error:" << e.what() << '\n';
    }catch(const std::bad_optional_access&){
        std::cerr << "Server had stopped, waiting detached works.\n";
    }
}

int main(int argc, char **argv){
    if(argc < 3){
        std::cout << "Usage: echo_server_coro <IP> <PORT>\n";
        return -1;
    }

    const std::string_view ip{argv[1]};
    const int port{std::atoi(argv[2])};

    asio_context ctx;
    ctx.start();

    ex::sync_wait(echo_server(ctx, ip, port));
}
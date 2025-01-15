#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/config.hpp>

#include <iostream>
#include <map>
#include <vector>
#include <utility>
#include <memory>
#include <thread>
#include <stdexcept>

#include "ortools/base/init_google.h"

#include "sorter.h"

/* Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
 * With modifications by Arnav Rawat
 *
 * Distributed under the Boost Software License, Version 1.0. (See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

template <class Body, class Allocator>
http::message_generator
handle_request(http::request<Body, http::basic_fields<Allocator>>&& req) {
    auto const bad_request = [&req](beast::string_view why) {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    if(req.method() != http::verb::post || req.target() != "/schedule")
        return bad_request("POST /schedule only");

    http::string_body::value_type body;

    try {
        Sorter sort(req.body());

        auto ret = sort.matchToSchedule();

        if (ret == std::nullopt) {
            body = "{}";
        } else {
            boost::json::object obj;
            for (auto& [id, vec] : ret.value()) {
                std::vector<boost::json::object> transformed;
                transformed.reserve(vec.size());

                std::transform(vec.begin(), vec.end(), std::back_inserter(transformed), [](std::tuple<int, int, int> tup) {
                    boost::json::object ret;
                    ret["id"] = std::get<0>(tup);
                    ret["start"] = std::get<1>(tup);
                    ret["end"] = std::get<2>(tup);
                    return ret;
                });
                obj[std::to_string(id)] = boost::json::value_from(transformed);
            }
            body = boost::json::serialize(obj);
        }
    } catch (std::exception e) {
        return bad_request("Invalid JSON");
    }

    auto const size = body.size();

    http::response<http::string_body> res {
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, req.version())
    };
    res.set(http::field::server, "lol");
    res.set(http::field::content_type, "application/json");
    res.content_length(size);
    res.keep_alive(false);
    return res;
}

void
fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

void
do_session(tcp::socket& socket)
{
    beast::error_code ec;

    beast::flat_buffer buffer;

    for(;;) {
        http::request<http::string_body> req;
        http::read(socket, buffer, req, ec);
        if(ec == http::error::end_of_stream)
            break;
        if(ec)
            return fail(ec, "read");

        http::message_generator msg =
            handle_request(std::move(req));

        bool keep_alive = msg.keep_alive();

        beast::write(socket, std::move(msg), ec);

        if(ec)
            return fail(ec, "write");
        if(!keep_alive)
            break;
    }

    socket.shutdown(tcp::socket::shutdown_send, ec);

}

int main(int argc, char* argv[])
{
    InitGoogle(argv[0], &argc, &argv, true);

    try {
        if (argc != 3) {
            std::cerr <<
                "Usage: sched_converter <address> <port>\n" <<
                "Example:\n" <<
                "\tsched_converter 0.0.0.0 8080 .\n";
            return EXIT_FAILURE;
        }
        auto const address = net::ip::make_address(argv[1]);
        auto const port = static_cast<unsigned short>(std::atoi(argv[2]));

        net::io_context ioc{1};

        tcp::acceptor acceptor{ioc, {address, port}};
        for(;;) {
            tcp::socket socket{ioc};

            acceptor.accept(socket);

            std::thread { std::bind(
                &do_session,
                std::move(socket)
            )}.detach();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}

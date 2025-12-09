
#ifndef WEBSOCKETCLIENT_HPP
#define WEBSOCKETCLIENT_HPP

#include "CommandProcessor.hpp"
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>

// ============================================================
// Boost.Beast WebSocket Client with SSL/WSS Support
// ============================================================
// This implementation uses Boost.Beast with OpenSSL for secure
// WebSocket connections (wss://).
// ============================================================

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

// Namespace Aliases
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();
    
    // ============================================================
    // Public Interface
    // ============================================================
    
    // Connects to the Server
    // Supports both ws:// (plain) and wss:// (SSL/TLS)
    void connect(const std::string& uri);
    
    // Sends a JSON response back to the server
    void send(const std::string& data);
    
    // Closes the connection cleanly
    void close();
    
    // Check if connection is open
    bool is_open() const;
    
private:
    // ============================================================
    // Internal Async Handlers
    // ============================================================
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep);
    void on_ssl_handshake(beast::error_code ec);  // NEW: SSL handshake
    void on_handshake(beast::error_code ec);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void on_close(beast::error_code ec);
    
    void do_read();
    void schedule_reconnect();
    void do_reconnect();
    bool parse_uri(const std::string& uri, std::string& host, std::string& port, std::string& path);

    // ============================================================
    // Member Variables
    // ============================================================
    
    net::io_context m_ioc;
    tcp::resolver m_resolver;
    
    // SSL Context for WSS connections
    ssl::context m_ssl_ctx{ssl::context::tlsv12_client};
    
    // Plain WebSocket stream (ws://)
    std::unique_ptr<websocket::stream<beast::tcp_stream>> m_ws;
    
    // Secure WebSocket stream (wss://)
    std::unique_ptr<websocket::stream<beast::ssl_stream<beast::tcp_stream>>> m_wss;
    
    // Flag to indicate if using SSL
    bool m_use_ssl;
    
    beast::flat_buffer m_buffer;
    
    // Connection state
    std::string m_host;
    std::string m_port;
    std::string m_path;
    std::string m_uri;
    
    // Reconnection settings
    int m_retry_count;
    int m_max_retries;
    int m_retry_delay_ms;
    int m_max_retry_delay_ms;
    std::unique_ptr<net::steady_timer> m_reconnect_timer;
    
    // Thread Safety
    bool m_open;
    bool m_should_reconnect;
    std::thread m_io_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
};

#endif // WEBSOCKETCLIENT_HPP
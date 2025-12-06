
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
// Boost.Beast WebSocket Client
// ============================================================
// This implementation uses Boost.Beast (part of Boost) instead of
// the separate WebSocket++ library. Beast is actively maintained
// and compatible with modern Boost versions (1.87+).
// ============================================================

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>

// Namespace Aliases (Makes code cleaner)
namespace beast = boost::beast;           // From <boost/beast.hpp>
namespace websocket = beast::websocket;   // From <boost/beast/websocket.hpp>
namespace net = boost::asio;              // From <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;         // From <boost/asio/ip/tcp.hpp>

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();
    
    // ============================================================
    // Public Interface (Same as before)
    // ============================================================
    
    // Connects to the Server (e.g., "ws://192.168.x.x:8000/ws/agent/")
    void connect(const std::string& uri);
    
    // Sends a JSON response back to the server
    void send(const std::string& data);
    
    // Closes the connection cleanly
    void close();
    
    // Check if connection is open
    bool is_open() const;
    
private:
    // ============================================================
    // Internal Async Handlers (Called by Beast)
    // ============================================================
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep);
    void on_handshake(beast::error_code ec);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void on_close(beast::error_code ec);
    
    // Start the async read loop
    void do_read();
    
    // Schedule a reconnection attempt after delay
    void schedule_reconnect();
    
    // Attempt to reconnect (called by timer)
    void do_reconnect();
    
    // Parse URI into components (host, port, path)
    bool parse_uri(const std::string& uri, std::string& host, std::string& port, std::string& path);

    // ============================================================
    // Member Variables
    // ============================================================
    
    // The io_context is the core I/O event loop (like the "Chef" analogy)
    net::io_context m_ioc;
    
    // Resolver: Translates hostname to IP address
    tcp::resolver m_resolver;
    
    // The WebSocket stream as unique_ptr (allows reset for reconnection)
    // Beast's websocket::stream has deleted move assignment, so we use ptr
    std::unique_ptr<websocket::stream<beast::tcp_stream>> m_ws;
    
    // Buffer for reading incoming messages
    beast::flat_buffer m_buffer;
    
    // Connection state info
    std::string m_host;
    std::string m_port;
    std::string m_path;
    std::string m_uri;                        // Full URI for reconnection
    
    // Reconnection settings
    int m_retry_count;                        // Current retry attempt
    int m_max_retries;                        // Max retries (0 = infinite)
    int m_retry_delay_ms;                     // Current delay in ms
    int m_max_retry_delay_ms;                 // Max delay cap
    std::unique_ptr<net::steady_timer> m_reconnect_timer;
    
    // Thread Safety
    bool m_open;
    bool m_should_reconnect;                  // Whether to attempt reconnection
    std::thread m_io_thread;                  // Background worker thread
    mutable std::mutex m_mutex;               // Lock for thread safety
    std::condition_variable m_cv;            // Signal for waiting threads
};

#endif // WEBSOCKETCLIENT_HPP

#include "WebSocketClient.hpp"
#include <iostream>
#include <sstream>
#include <regex>
#include <nlohmann/json.hpp>

// ============================================================
// Constructor
// ============================================================
// The initialization list initializes Beast components in order:
// 1. m_ioc      - The I/O context (event loop)
// 2. m_resolver - Needs a reference to the I/O context
// 3. m_ws       - Uses beast::tcp_stream for timeout support
// ============================================================
WebSocketClient::WebSocketClient()
    : m_resolver(net::make_strand(m_ioc))
    , m_ws(std::make_unique<websocket::stream<beast::tcp_stream>>(net::make_strand(m_ioc)))
    , m_open(false)
    , m_should_reconnect(true)
    , m_retry_count(0)
    , m_max_retries(0)           // 0 = infinite retries
    , m_retry_delay_ms(5000)     // Start with 5 second delay
    , m_max_retry_delay_ms(60000) // Max 60 second delay
{
    std::cout << "[WebSocket] Beast client initialized (auto-reconnect enabled)." << std::endl;
}

WebSocketClient::~WebSocketClient() {
    close();
}

// ============================================================
// URI Parser
// ============================================================
// Parses "ws://host:port/path" or "wss://host:port/path"
// Returns false if the URI format is invalid.
// ============================================================
bool WebSocketClient::parse_uri(const std::string& uri, 
                                 std::string& host, 
                                 std::string& port, 
                                 std::string& path) {
    // Simple regex for ws:// or wss:// URIs
    std::regex uri_regex(R"(wss?://([^:/]+)(?::(\d+))?(/.*)?)");
    std::smatch match;
    
    if (!std::regex_match(uri, match, uri_regex)) {
        std::cerr << "[WebSocket] Invalid URI format: " << uri << std::endl;
        return false;
    }
    
    host = match[1].str();
    port = match[2].matched ? match[2].str() : "80";  // Default to 80 if no port
    path = match[3].matched ? match[3].str() : "/";   // Default to / if no path
    
    // If wss://, default to 443
    if (uri.substr(0, 4) == "wss:" && !match[2].matched) {
        port = "443";
    }
    
    return true;
}

// ============================================================
// Connect
// ============================================================
// 1. Parse the URI
// 2. Start async DNS resolution
// 3. Start the I/O thread
// ============================================================
void WebSocketClient::connect(const std::string& uri) {
    // Store URI for potential reconnection
    m_uri = uri;
    
    if (!parse_uri(uri, m_host, m_port, m_path)) {
        return;
    }
    
    std::cout << "[WebSocket] Connecting to " << m_host << ":" << m_port << m_path << std::endl;
    
    // Start the async resolution chain
    // Each step calls the next: resolve -> connect -> handshake -> read
    m_resolver.async_resolve(
        m_host,
        m_port,
        beast::bind_front_handler(&WebSocketClient::on_resolve, this)
    );
    
    // Start the I/O thread (the "Chef" who processes the queue) if not already running
    if (!m_io_thread.joinable()) {
        m_io_thread = std::thread([this]() {
            m_ioc.run();
        });
    }
}

// ============================================================
// Async Handler: on_resolve
// ============================================================
// Called when DNS resolution completes.
// Next step: Connect to the resolved IP address.
// ============================================================
void WebSocketClient::on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (ec) {
        std::cerr << "[WebSocket] Resolve error: " << ec.message() << std::endl;
        schedule_reconnect();
        return;
    }
    
    // Set a timeout for the TCP connect operation
    // beast::tcp_stream supports expires_after (raw tcp::socket doesn't)
    beast::get_lowest_layer(*m_ws).expires_after(std::chrono::seconds(30));
    
    // Connect to the IP address using the results
    beast::get_lowest_layer(*m_ws).async_connect(
        results,
        beast::bind_front_handler(&WebSocketClient::on_connect, this)
    );
}

// ============================================================
// Async Handler: on_connect
// ============================================================
// Called when TCP connection is established.
// Next step: Perform WebSocket handshake.
// ============================================================
void WebSocketClient::on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
    if (ec) {
        std::cerr << "[WebSocket] Connect error: " << ec.message() << std::endl;
        schedule_reconnect();
        return;
    }
    
    std::cout << "[WebSocket] TCP connected to " << ep << std::endl;
    
    // Turn off the timeout for the handshake (it has its own timeout)
    beast::get_lowest_layer(*m_ws).expires_never();
    
    // Set suggested timeout settings for the websocket
    m_ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    
    // Set a decorator to change the User-Agent
    m_ws->set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req) {
            req.set(beast::http::field::user_agent, "EDR-Agent/1.0");
        }
    ));
    
    // Perform the WebSocket handshake
    // The host string is used in the HTTP "Host" header
    m_ws->async_handshake(
        m_host,
        m_path,
        beast::bind_front_handler(&WebSocketClient::on_handshake, this)
    );
}

// ============================================================
// Async Handler: on_handshake
// ============================================================
// Called when WebSocket handshake completes.
// Next step: Start reading messages.
// ============================================================
void WebSocketClient::on_handshake(beast::error_code ec) {
    if (ec) {
        std::cerr << "[WebSocket] Handshake error: " << ec.message() << std::endl;
        schedule_reconnect();
        return;
    }
    
    std::cout << "[WebSocket] Connected successfully!" << std::endl;
    
    // Mark connection as open and reset retry state on success
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_open = true;
        // Reset retry counter and delay on successful connection
        m_retry_count = 0;
        m_retry_delay_ms = 5000;  // Reset to initial delay
    }
    m_cv.notify_all();
    
    // Start the read loop
    do_read();
}

// ============================================================
// do_read - Start Async Read
// ============================================================
void WebSocketClient::do_read() {
    // Clear the buffer before reading
    m_buffer.consume(m_buffer.size());
    
    // Read a message into the buffer
    m_ws->async_read(
        m_buffer,
        beast::bind_front_handler(&WebSocketClient::on_read, this)
    );
}

// ============================================================
// Async Handler: on_read
// ============================================================
// Called when a message is received from the server.
// Processes the command and sends a response.
// ============================================================
void WebSocketClient::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    
    // Handle connection closed normally
    if (ec == websocket::error::closed) {
        std::cout << "[WebSocket] Connection closed by server." << std::endl;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_open = false;
        }
        schedule_reconnect();
        return;
    }
    
    // Handle other errors
    if (ec) {
        std::cerr << "[WebSocket] Read error: " << ec.message() << std::endl;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_open = false;
        }
        schedule_reconnect();
        return;
    }
    
    // Extract the message as a string
    std::string message = beast::buffers_to_string(m_buffer.data());
    std::cout << "[WebSocket] Received: " << message << std::endl;
    



try {
        nlohmann::json data = nlohmann::json::parse(message);
        std::string msgType = data.value("type", "");
        
        if (msgType == "command") {
            // It's a command - process it
            std::cout << "[WebSocket] Processing command..." << std::endl;
            std::string response = CommandProcessor::executeCommand(message);
            if (!response.empty()) {
                send(response);
            }
        } else if (msgType == "connection_established") {
            // Welcome message - just log it
            std::cout << "[WebSocket] Server says: " << data.value("message", "") << std::endl;
        } else if (msgType == "heartbeat_ack") {
            // Heartbeat acknowledgment
            std::cout << "[WebSocket] Heartbeat acknowledged" << std::endl;
        } else {
            // Unknown type - log but don't send error
            std::cout << "[WebSocket] Ignoring message type: " << msgType << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] JSON parse error: " << e.what() << std::endl;
    }   
    
    // Continue reading
    do_read();
}

// ============================================================
// Send
// ============================================================
void WebSocketClient::send(const std::string& data) {
    // Check if connected
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_open) {
            std::cerr << "[WebSocket] Cannot send: Not connected." << std::endl;
            return;
        }
    }
    
    // Use post to ensure thread safety
    net::post(m_ws->get_executor(), [this, data]() {
        beast::error_code ec;
        
        // Set text mode for JSON
        m_ws->text(true);
        
        // Synchronous write (simpler for our use case)
        m_ws->write(net::buffer(data), ec);
        
        if (ec) {
            std::cerr << "[WebSocket] Write error: " << ec.message() << std::endl;
        } else {
            std::cout << "[WebSocket] Sent: " << data.substr(0, 100) << "..." << std::endl;
        }
    });
}

// ============================================================
// Async Handler: on_write
// ============================================================
void WebSocketClient::on_write(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    
    if (ec) {
        std::cerr << "[WebSocket] Write error: " << ec.message() << std::endl;
    }
}

// ============================================================
// Close
// ============================================================
void WebSocketClient::close() {
    // Disable reconnection - user explicitly wants to close
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_should_reconnect = false;
    }
    
    // Cancel any pending reconnect timer
    if (m_reconnect_timer) {
        m_reconnect_timer->cancel();
    }
    
    // Check if already closed
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_open) {
            // Just make sure thread is cleaned up
            if (m_io_thread.joinable()) {
                m_ioc.stop();
                m_io_thread.join();
            }
            return;
        }
        m_open = false;
    }
    
    std::cout << "[WebSocket] Closing connection..." << std::endl;
    
    // Post the close to the I/O thread
    net::post(m_ws->get_executor(), [this]() {
        beast::error_code ec;
        m_ws->close(websocket::close_code::normal, ec);
        if (ec) {
            std::cerr << "[WebSocket] Close error: " << ec.message() << std::endl;
        }
    });
    
    // Stop the I/O context and wait for the thread
    m_ioc.stop();
    if (m_io_thread.joinable()) {
        m_io_thread.join();
    }
    
    std::cout << "[WebSocket] Connection closed." << std::endl;
}

// ============================================================
// Async Handler: on_close
// ============================================================
void WebSocketClient::on_close(beast::error_code ec) {
    if (ec) {
        std::cerr << "[WebSocket] Close error: " << ec.message() << std::endl;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    m_open = false;
}

// ============================================================
// is_open
// ============================================================
bool WebSocketClient::is_open() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_open;
}

// ============================================================
// Reconnection Logic
// ============================================================

// Schedule a reconnection attempt after a delay (exponential backoff)
void WebSocketClient::schedule_reconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Check if reconnection is disabled
    if (!m_should_reconnect) {
        std::cout << "[WebSocket] Reconnection disabled, not retrying." << std::endl;
        return;
    }
    
    // Check max retries (0 = infinite)
    if (m_max_retries > 0 && m_retry_count >= m_max_retries) {
        std::cerr << "[WebSocket] Max retries reached (" << m_max_retries << "), giving up." << std::endl;
        return;
    }
    
    m_retry_count++;
    int delay_seconds = m_retry_delay_ms / 1000;
    
    std::cout << "[WebSocket] Scheduling reconnection attempt #" << m_retry_count 
              << " in " << delay_seconds << " seconds..." << std::endl;
    
    // Create or reset the timer
    if (!m_reconnect_timer) {
        m_reconnect_timer = std::make_unique<net::steady_timer>(m_ioc);
    }
    
    m_reconnect_timer->expires_after(std::chrono::milliseconds(m_retry_delay_ms));
    m_reconnect_timer->async_wait([this](beast::error_code ec) {
        if (!ec) {
            do_reconnect();
        }
    });
    
    // Exponential backoff: double the delay (capped at max)
    m_retry_delay_ms = std::min(m_retry_delay_ms * 2, m_max_retry_delay_ms);
}

// Actually attempt the reconnection
void WebSocketClient::do_reconnect() {
    std::cout << "[WebSocket] Attempting reconnection to " << m_host << ":" << m_port << m_path << std::endl;
    
    // Reset the WebSocket stream for a fresh connection
    // Using reset() because unique_ptr allows this (unlike direct assignment)
    m_ws.reset(new websocket::stream<beast::tcp_stream>(net::make_strand(m_ioc)));
    
    // Start the resolution chain again
    m_resolver.async_resolve(
        m_host,
        m_port,
        beast::bind_front_handler(&WebSocketClient::on_resolve, this)
    );
}
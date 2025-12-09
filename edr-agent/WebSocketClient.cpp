
#include "WebSocketClient.hpp"
#include "Logger.hpp"
#include <iostream>
#include <sstream>
#include <regex>
#include <nlohmann/json.hpp>
#include <openssl/ssl.h>  // For SSL_set_tlsext_host_name (SNI)

// ============================================================
// Constructor
// ============================================================
WebSocketClient::WebSocketClient()
    : m_resolver(net::make_strand(m_ioc))
    , m_ssl_ctx(ssl::context::tlsv12_client)
    , m_ws(nullptr)
    , m_wss(nullptr)
    , m_use_ssl(false)
    , m_open(false)
    , m_should_reconnect(true)
    , m_retry_count(0)
    , m_max_retries(0)
    , m_retry_delay_ms(5000)
    , m_max_retry_delay_ms(60000)
{
    // Configure SSL context for WSS connections
    m_ssl_ctx.set_default_verify_paths();
    m_ssl_ctx.set_verify_mode(ssl::verify_peer);
    
    LOG_INFO("WebSocket client initialized (ws/wss supported)");
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
        LOG_ERROR("Invalid WebSocket URI format: " + uri);
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
void WebSocketClient::connect(const std::string& uri) {
    m_uri = uri;
    
    if (!parse_uri(uri, m_host, m_port, m_path)) {
        return;
    }
    
    // Detect SSL from URI scheme
    m_use_ssl = (uri.substr(0, 4) == "wss:");
    
    LOG_INFO("WebSocket connecting to " + m_host + ":" + m_port + m_path + 
             (m_use_ssl ? " (SSL/TLS)" : " (plain)"));
    
    // Create appropriate stream type
    if (m_use_ssl) {
        m_wss = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(
            net::make_strand(m_ioc), m_ssl_ctx);
        m_ws.reset();
    } else {
        m_ws = std::make_unique<websocket::stream<beast::tcp_stream>>(
            net::make_strand(m_ioc));
        m_wss.reset();
    }
    
    // Start async DNS resolution
    m_resolver.async_resolve(
        m_host,
        m_port,
        beast::bind_front_handler(&WebSocketClient::on_resolve, this)
    );
    
    // Start I/O thread if not running
    if (!m_io_thread.joinable()) {
        m_io_thread = std::thread([this]() {
            m_ioc.run();
        });
    }
}

// ============================================================
// Async Handler: on_resolve
// ============================================================
void WebSocketClient::on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (ec) {
        LOG_ERROR("WebSocket resolve error: " + ec.message());
        schedule_reconnect();
        return;
    }
    
    // Set timeout and connect based on stream type
    if (m_use_ssl && m_wss) {
        beast::get_lowest_layer(*m_wss).expires_after(std::chrono::seconds(30));
        beast::get_lowest_layer(*m_wss).async_connect(
            results,
            beast::bind_front_handler(&WebSocketClient::on_connect, this)
        );
    } else if (m_ws) {
        beast::get_lowest_layer(*m_ws).expires_after(std::chrono::seconds(30));
        beast::get_lowest_layer(*m_ws).async_connect(
            results,
            beast::bind_front_handler(&WebSocketClient::on_connect, this)
        );
    }
}

// ============================================================
// Async Handler: on_connect
// ============================================================
void WebSocketClient::on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
    if (ec) {
        LOG_ERROR("WebSocket connect error: " + ec.message());
        schedule_reconnect();
        return;
    }
    
    LOG_INFO("WebSocket TCP connected");
    
    if (m_use_ssl && m_wss) {
        // For WSS: First do SSL handshake, then WebSocket handshake
        beast::get_lowest_layer(*m_wss).expires_never();
        
        // Set SNI hostname (required for most servers)
        if (!SSL_set_tlsext_host_name(m_wss->next_layer().native_handle(), m_host.c_str())) {
            LOG_ERROR("Failed to set SNI hostname");
            schedule_reconnect();
            return;
        }
        
        // Perform SSL handshake
        m_wss->next_layer().async_handshake(
            ssl::stream_base::client,
            beast::bind_front_handler(&WebSocketClient::on_ssl_handshake, this)
        );
    } else if (m_ws) {
        // For WS: Go directly to WebSocket handshake
        beast::get_lowest_layer(*m_ws).expires_never();
        m_ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        m_ws->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent, "EDR-Agent/1.0");
            }
        ));
        m_ws->async_handshake(
            m_host,
            m_path,
            beast::bind_front_handler(&WebSocketClient::on_handshake, this)
        );
    }
}

// ============================================================
// Async Handler: on_ssl_handshake (NEW for WSS)
// ============================================================
void WebSocketClient::on_ssl_handshake(beast::error_code ec) {
    if (ec) {
        LOG_ERROR("SSL handshake error: " + ec.message());
        schedule_reconnect();
        return;
    }
    
    LOG_INFO("SSL handshake completed");
    
    // Now do WebSocket handshake over SSL
    m_wss->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    m_wss->set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req) {
            req.set(beast::http::field::user_agent, "EDR-Agent/1.0");
        }
    ));
    m_wss->async_handshake(
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
        LOG_ERROR("WebSocket handshake error: " + ec.message());
        schedule_reconnect();
        return;
    }
    
    LOG_INFO("WebSocket connected successfully!");
    
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
    m_buffer.consume(m_buffer.size());
    
    if (m_use_ssl && m_wss) {
        m_wss->async_read(
            m_buffer,
            beast::bind_front_handler(&WebSocketClient::on_read, this)
        );
    } else if (m_ws) {
        m_ws->async_read(
            m_buffer,
            beast::bind_front_handler(&WebSocketClient::on_read, this)
        );
    }
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
        LOG_WARN("WebSocket connection closed by server");
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_open = false;
        }
        schedule_reconnect();
        return;
    }
    
    // Handle other errors
    if (ec) {
        LOG_ERROR("WebSocket read error: " + ec.message());
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_open = false;
        }
        schedule_reconnect();
        return;
    }
    
    // Extract the message as a string
    std::string message = beast::buffers_to_string(m_buffer.data());
    LOG_DEBUG("WebSocket received: " + message.substr(0, std::min((size_t)100, message.size())) + "...");
    



try {
        nlohmann::json data = nlohmann::json::parse(message);
        std::string msgType = data.value("type", "");
        
        if (msgType == "command") {
            // It's a command - process it
            LOG_DEBUG("WebSocket processing command...");
            std::string response = CommandProcessor::executeCommand(message);
            if (!response.empty()) {
                send(response);
            }
        } else if (msgType == "connection_established") {
            // Welcome message - just log it
            LOG_INFO("WebSocket server: " + data.value("message", std::string("")));
        } else if (msgType == "heartbeat_ack") {
            // Heartbeat acknowledgment
            LOG_DEBUG("WebSocket heartbeat acknowledged");
        } else {
            // Unknown type - log but don't send error
            LOG_DEBUG("WebSocket ignoring message type: " + msgType);
        }
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("WebSocket JSON parse error: ") + e.what());
    }   
    
    // Continue reading
    do_read();
}

// ============================================================
// Send
// ============================================================
void WebSocketClient::send(const std::string& data) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_open) {
            LOG_WARN("WebSocket cannot send: Not connected");
            return;
        }
    }
    
    if (m_use_ssl && m_wss) {
        net::post(m_wss->get_executor(), [this, data]() {
            beast::error_code ec;
            m_wss->text(true);
            m_wss->write(net::buffer(data), ec);
            if (ec) {
                LOG_ERROR("WebSocket write error: " + ec.message());
            } else {
                LOG_DEBUG("WebSocket sent: " + data.substr(0, std::min((size_t)100, data.size())) + "...");
            }
        });
    } else if (m_ws) {
        net::post(m_ws->get_executor(), [this, data]() {
            beast::error_code ec;
            m_ws->text(true);
            m_ws->write(net::buffer(data), ec);
            if (ec) {
                LOG_ERROR("WebSocket write error: " + ec.message());
            } else {
                LOG_DEBUG("WebSocket sent: " + data.substr(0, std::min((size_t)100, data.size())) + "...");
            }
        });
    }
}

// ============================================================
// Async Handler: on_write
// ============================================================
void WebSocketClient::on_write(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    
    if (ec) {
        LOG_ERROR("WebSocket write error: " + ec.message());
    }
}

// ============================================================
// ============================================================
// Close
// ============================================================
void WebSocketClient::close() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_should_reconnect = false;
    }
    
    if (m_reconnect_timer) {
        m_reconnect_timer->cancel();
    }
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_open) {
            if (m_io_thread.joinable()) {
                m_ioc.stop();
                m_io_thread.join();
            }
            return;
        }
        m_open = false;
    }
    
    LOG_INFO("WebSocket closing connection...");
    
    // Close appropriate stream
    if (m_use_ssl && m_wss) {
        net::post(m_wss->get_executor(), [this]() {
            beast::error_code ec;
            m_wss->close(websocket::close_code::normal, ec);
            if (ec) {
                LOG_ERROR("WebSocket close error: " + ec.message());
            }
        });
    } else if (m_ws) {
        net::post(m_ws->get_executor(), [this]() {
            beast::error_code ec;
            m_ws->close(websocket::close_code::normal, ec);
            if (ec) {
                LOG_ERROR("WebSocket close error: " + ec.message());
            }
        });
    }
    
    m_ioc.stop();
    if (m_io_thread.joinable()) {
        m_io_thread.join();
    }
    
    LOG_INFO("WebSocket connection closed");
}

// ============================================================
// Async Handler: on_close
// ============================================================
void WebSocketClient::on_close(beast::error_code ec) {
    if (ec) {
        LOG_ERROR("WebSocket close error: " + ec.message());
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
        LOG_INFO("WebSocket reconnection disabled, not retrying");
        return;
    }
    
    // Check max retries (0 = infinite)
    if (m_max_retries > 0 && m_retry_count >= m_max_retries) {
        LOG_ERROR("WebSocket max retries reached (" + std::to_string(m_max_retries) + "), giving up");
        return;
    }
    
    m_retry_count++;
    int delay_seconds = m_retry_delay_ms / 1000;
    
    LOG_INFO("WebSocket scheduling reconnection #" + std::to_string(m_retry_count) + " in " + std::to_string(delay_seconds) + " seconds...");
    
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
    LOG_INFO("WebSocket attempting reconnection to " + m_host + ":" + m_port + m_path);
    
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
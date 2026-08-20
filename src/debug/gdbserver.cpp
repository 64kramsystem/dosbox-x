#include "dosbox.h"

#if C_REMOTEDEBUG

#include <algorithm>
#include <errno.h>
#include <vector>
#include "gdbserver.h"
#include "debug.h"
#include "logging.h"

// Helper functions
static inline uint32_t swap32(uint32_t x) {
    return (((x >> 24) & 0x000000ff) |
            ((x >> 8) & 0x0000ff00) |
            ((x << 8) & 0x00ff0000) |
            ((x << 24) & 0xff000000));
}

void GDBServer::start() {
    if (running) {
        LOG(LOG_REMOTE, LOG_WARN)("GDBServer: Already running");
        return;
    }
    running = setup_socket();
}

void GDBServer::stop() {
    if (!running) return;

    LOG(LOG_REMOTE, LOG_NORMAL)("GDBServer: Stopping...");
    running = false;

    drop_client();
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
}

bool GDBServer::setup_socket() {
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG(LOG_REMOTE, LOG_ERROR)("GDBServer: socket failed: %s", strerror(errno));
        return false;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG(LOG_REMOTE, LOG_ERROR)("GDBServer: setsockopt failed: %s", strerror(errno));
        close(server_fd);
        server_fd = -1;
        return false;
    }

    // Set non-blocking
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        LOG(LOG_REMOTE, LOG_ERROR)("GDBServer: bind failed: %s", strerror(errno));
        close(server_fd);
        server_fd = -1;
        return false;
    }

    if (listen(server_fd, 1) < 0) {
        LOG(LOG_REMOTE, LOG_ERROR)("GDBServer: listen failed: %s", strerror(errno));
        close(server_fd);
        server_fd = -1;
        return false;
    }

    LOG(LOG_REMOTE, LOG_NORMAL)("GDBServer: Listening on port %d", port);
    return true;
}

bool GDBServer::try_accept() {
    if (server_fd < 0) return false;

    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    int new_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen);
    if (new_fd < 0) {
        if (errno == EAGAIN
#if EWOULDBLOCK != EAGAIN
            || errno == EWOULDBLOCK
#endif
        ) {
            return false;  // No pending connection
        }
        LOG(LOG_REMOTE, LOG_ERROR)("GDBServer: accept failed: %s", strerror(errno));
        return false;
    }

    // Replies are sent from the emulation thread and must never block it
    const int client_flags = fcntl(new_fd, F_GETFL, 0);
    fcntl(new_fd, F_SETFL, client_flags | O_NONBLOCK);

    client_fd = new_fd;
    recv_buffer.clear();
    send_buffer.clear();
    send_failed = false;
    noack_mode = false;

    LOG(LOG_REMOTE, LOG_NORMAL)("GDBServer: Client connected");
    return true;
}

GDBAction GDBServer::poll() {
    if (!running) return GDBAction::NONE;

    // Try to accept new client if we don't have one
    if (client_fd < 0) {
        try_accept();
        return GDBAction::NONE;
    }

    // Read any available data and send queued replies
    if (send_failed || !flush_send_buffer() || !receive_data()) {
        // Client disconnected
        LOG(LOG_REMOTE, LOG_NORMAL)("GDBServer: Client disconnected");
        drop_client();
        return GDBAction::DISCONNECT;
    }

    // Process complete packets
    while (has_complete_packet()) {
        std::string packet = extract_packet();
        if (packet.empty()) continue;

        GDBAction action = process_command(packet);
        if (action != GDBAction::NONE) {
            return action;
        }
    }

    return GDBAction::NONE;
}

bool GDBServer::receive_data() {
    char buf[1024];
    while (true) {
        ssize_t n = recv(client_fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0) {
            recv_buffer.append(buf, n);
            if (recv_buffer.size() > 65536) {
                LOG(LOG_REMOTE, LOG_ERROR)("GDBServer: receive buffer limit exceeded");
                return false;
            }
        } else if (n == 0) {
            // Connection closed
            return false;
        } else {
            if (errno == EAGAIN
#if EWOULDBLOCK != EAGAIN
                || errno == EWOULDBLOCK
#endif
            ) {
                // No more data available
                return true;
            }
            // Real error
            LOG(LOG_REMOTE, LOG_ERROR)("GDBServer: read error: %s", strerror(errno));
            return false;
        }
    }
}

void GDBServer::drop_client() {
    if (client_fd >= 0) close(client_fd);
    client_fd = -1;
    recv_buffer.clear();
    send_buffer.clear();
    send_failed = false;
    noack_mode = false;
}

// Queues data and sends what the socket accepts without blocking; poll()
// sends the rest. A client that stops reading is dropped at the next poll.
bool GDBServer::send_bytes(const char* data, size_t size) {
    if (client_fd < 0 || send_failed) return false;
    const size_t max_pending = 1024 * 1024;
    if (send_buffer.size() + size > max_pending) {
        LOG(LOG_REMOTE, LOG_ERROR)("GDBServer: client is not reading replies");
        send_failed = true;
        return false;
    }
    send_buffer.append(data, size);
    if (!flush_send_buffer()) {
        send_failed = true;
        return false;
    }
    return true;
}

bool GDBServer::flush_send_buffer() {
    size_t sent = 0;
    while (sent < send_buffer.size()) {
        const ssize_t count = send(client_fd, send_buffer.data() + sent,
                                   send_buffer.size() - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (count > 0) {
            sent += static_cast<size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else if (count < 0 && (errno == EAGAIN
#if EWOULDBLOCK != EAGAIN
                                 || errno == EWOULDBLOCK
#endif
                                 )) {
            break;
        } else {
            send_buffer.erase(0, sent);
            return false;
        }
    }
    send_buffer.erase(0, sent);
    return true;
}

bool GDBServer::has_complete_packet() const {
    // Check for Ctrl-C (0x03)
    if (!recv_buffer.empty() && recv_buffer[0] == 0x03) {
        return true;
    }

    // Check for complete packet: $...#xx
    size_t start = recv_buffer.find('$');
    if (start == std::string::npos) {
        return false;
    }

    size_t hash = recv_buffer.find('#', start);
    if (hash == std::string::npos) {
        return false;
    }

    // Need 2 more chars for checksum
    return recv_buffer.length() >= hash + 3;
}

std::string GDBServer::extract_packet() {
    // Handle Ctrl-C (interrupt)
    if (!recv_buffer.empty() && recv_buffer[0] == 0x03) {
        recv_buffer.erase(0, 1);
        LOG(LOG_REMOTE, LOG_DEBUG)("GDBServer: Received interrupt (Ctrl-C)");
        return "\x03";  // Return special marker
    }

    // Find packet boundaries
    size_t start = recv_buffer.find('$');
    if (start == std::string::npos) {
        recv_buffer.clear();  // Discard garbage
        return "";
    }

    // Discard anything before '$'
    if (start > 0) {
        recv_buffer.erase(0, start);
        start = 0;
    }

    size_t hash = recv_buffer.find('#', start);
    if (hash == std::string::npos || recv_buffer.length() < hash + 3) {
        return "";  // Incomplete
    }

    // Extract packet content (between $ and #)
    std::string packet = recv_buffer.substr(start + 1, hash - start - 1);

    // Extract and verify checksum
    std::string checksum_str = recv_buffer.substr(hash + 1, 2);
    uint8_t received_checksum = (hex_to_int(checksum_str[0]) << 4) | hex_to_int(checksum_str[1]);

    uint8_t calculated_checksum = 0;
    for (char c : packet) {
        calculated_checksum += static_cast<uint8_t>(c);
    }

    // Remove packet from buffer
    recv_buffer.erase(0, hash + 3);

    if (received_checksum != calculated_checksum) {
        LOG(LOG_REMOTE, LOG_WARN)("GDBServer: Checksum mismatch! received 0x%02x, calculated 0x%02x",
                                  received_checksum, calculated_checksum);
        if (!noack_mode) {
            send_bytes("-", 1);
        }
        return "";
    }

    // Send ACK
    if (!noack_mode) {
        send_bytes("+", 1);
    }

    LOG(LOG_REMOTE, LOG_DEBUG)("GDBServer: << %s", packet.c_str());
    return packet;
}

void GDBServer::send_packet(const std::string& packet) {
    if (client_fd < 0) return;

    LOG(LOG_REMOTE, LOG_DEBUG)("GDBServer: >> %s", packet.c_str());

    uint8_t checksum = 0;
    for (char c : packet) {
        checksum += static_cast<uint8_t>(c);
    }

    char suffix[4];
    snprintf(suffix, sizeof(suffix), "#%02x", checksum);
    const std::string response = "$" + packet + suffix;
    send_bytes(response.data(), response.size());

    // In non-blocking mode, we don't wait for ACK synchronously
    // The ACK will be in recv_buffer on next poll()
    // For simplicity, we just ignore ACKs (they're discarded in extract_packet)
}

void GDBServer::send_stop_reply() {
    send_packet("S05");  // SIGTRAP
}

GDBAction GDBServer::process_command(const std::string& cmd) {
    try {
    // Handle Ctrl-C interrupt
    if (cmd == "\x03") {
        // Already stopped: the stop reply was sent, a second one would shift replies
        if (DEBUG_IsGDBStopped()) return GDBAction::NONE;
        LOG(LOG_REMOTE, LOG_NORMAL)("GDBServer: Ctrl-C received, stopping CPU");
        send_stop_reply();
        return GDBAction::STOP;  // Tell debug.cpp to pause CPU
    }

    if (cmd == "QStartNoAckMode") {
        noack_mode = true;
        send_packet("OK");
    } else if (cmd == "vMustReplyEmpty") {
        send_packet("");
    } else if (cmd == "?") {
        // Query halt reason - GDB wants us stopped
        LOG(LOG_REMOTE, LOG_NORMAL)("GDBServer: Halt reason query, stopping CPU");
        send_stop_reply();
        return GDBAction::STOP;  // Tell debug.cpp to pause CPU
    } else if (cmd.substr(0, 1) == "H") {
        send_packet("OK");
    } else if (cmd.substr(0, 1) == "p") {
        handle_read_register(cmd);
    } else if (cmd == "g") {
        handle_read_registers();
    } else if (cmd.substr(0, 1) == "G") {
        handle_write_registers(cmd.substr(1));
    } else if (cmd.substr(0, 1) == "m") {
        handle_read_memory(cmd.substr(1));
    } else if (cmd.substr(0, 1) == "M") {
        handle_write_memory(cmd.substr(1));
    } else if (cmd.substr(0, 1) == "Z" || cmd.substr(0, 1) == "z") {
        handle_breakpoint(cmd);
    } else if (cmd.substr(0, 1) == "s") {
        // Step - return action, debugger will call send_stop_reply() when done
        return GDBAction::STEP;
    } else if (cmd.substr(0, 1) == "c") {
        // Continue - return action, debugger will call send_stop_reply() on breakpoint
        return GDBAction::CONTINUE;
    } else if (cmd.substr(0, 1) == "q") {
        handle_query(cmd.substr(1));
    } else if (cmd.substr(0, 5) == "vCont") {
        return handle_v_packets(cmd);
    } else if (cmd == "D" || cmd.substr(0, 2) == "D;") {
        LOG(LOG_REMOTE, LOG_NORMAL)("GDBServer: Client detaching");
        send_packet("OK");
        flush_send_buffer();
        drop_client();
        return GDBAction::DISCONNECT;
    } else {
        LOG(LOG_REMOTE, LOG_DEBUG)("GDBServer: Unhandled command: %s", cmd.c_str());
        send_packet("");
    }

    } catch (const std::exception& error) {
        LOG(LOG_REMOTE, LOG_WARN)("GDBServer: Invalid command: %s", error.what());
        send_packet("E01");
    }
    return GDBAction::NONE;
}

GDBAction GDBServer::handle_v_packets(const std::string& cmd) {
    if (cmd == "vCont?") {
        send_packet("vCont;c;s");
        return GDBAction::NONE;
    }

    if (cmd.length() >= 7 && cmd.substr(0, 6) == "vCont;") {
        char action = cmd[6];
        switch (action) {
            case 'c':
                return GDBAction::CONTINUE;
            case 's':
                return GDBAction::STEP;
            default:
                send_packet("");
                return GDBAction::NONE;
        }
    }

    send_packet("");
    return GDBAction::NONE;
}

void GDBServer::handle_read_register(const std::string& cmd) {
    int reg_num = std::stoi(cmd.substr(1), nullptr, 16);
    uint32_t value = DEBUG_GetRegister(reg_num);

    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(8) << swap32(value);
    send_packet(ss.str());
}

void GDBServer::handle_read_registers() {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    // x86 32-bit register order: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI, EIP, EFLAGS, CS, SS, DS, ES, FS, GS
    const int reg_count = 16;
    for (int i = 0; i < reg_count; ++i) {
        uint32_t value = DEBUG_GetRegister(i);
        ss << std::setw(8) << swap32(value);
    }

    send_packet(ss.str());
}

void GDBServer::handle_write_registers(const std::string& args) {
    if (args.empty() || args.size() > 16 * 8 || args.size() % 8 != 0) {
        send_packet("E01");
        return;
    }
    // Parse hex string, 8 chars per register
    const size_t register_count = std::min<size_t>(16, args.length() / 8);
    std::vector<uint32_t> values;
    values.reserve(register_count);
    for (size_t i = 0; i < register_count; ++i) {
        std::string hex_val = args.substr(i * 8, 8);
        uint32_t value = std::stoul(hex_val, nullptr, 16);
        values.push_back(swap32(value));
    }
    // Validate everything first, so a rejected value changes nothing
    for (size_t i = 0; i < values.size(); ++i) {
        if (!DEBUG_CanSetRegister(static_cast<int>(i), values[i])) {
            send_packet("E01");
            return;
        }
    }
    // CS determines how the linear program counter maps back to EIP, so write
    // EIP last if GDB supplied a complete register set.
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 8) DEBUG_SetRegister(static_cast<int>(i), values[i]);
    }
    if (values.size() > 8) DEBUG_SetRegister(8, values[8]);
    send_packet("OK");
}

void GDBServer::handle_read_memory(const std::string& args) {
    size_t comma = args.find(',');
    if (comma == std::string::npos) {
        send_packet("E01");
        return;
    }

    uint32_t address = std::stoul(args.substr(0, comma), nullptr, 16);
    uint32_t length = std::stoul(args.substr(comma + 1), nullptr, 16);
    if (length > 0x1fff || address + static_cast<uint64_t>(length) > 0x100000000ULL) {
        send_packet("E01");
        return;
    }

    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    for (uint32_t i = 0; i < length; ++i) {
        uint8_t value = DEBUG_ReadMemory(address + i);
        ss << std::setw(2) << static_cast<int>(value);
    }

    send_packet(ss.str());
}

void GDBServer::handle_write_memory(const std::string& args) {
    size_t comma = args.find(',');
    size_t colon = args.find(':');
    if (comma == std::string::npos || colon == std::string::npos) {
        send_packet("E01");
        return;
    }

    uint32_t address = std::stoul(args.substr(0, comma), nullptr, 16);
    uint32_t length = std::stoul(args.substr(comma + 1, colon - comma - 1), nullptr, 16);
    std::string data = hex_decode(args.substr(colon + 1));
    if (length > 0x1fff || data.size() != length ||
        address + static_cast<uint64_t>(length) > 0x100000000ULL) {
        send_packet("E01");
        return;
    }

    for (size_t i = 0; i < data.length(); ++i) {
        DEBUG_WriteMemory(address + static_cast<uint32_t>(i), data[i]);
    }

    send_packet("OK");
}

void GDBServer::handle_breakpoint(const std::string& args) {
    char type = args[0];
    size_t comma1 = args.find(',');
    size_t comma2 = args.find(',', comma1 + 1);
    if (comma1 == std::string::npos || comma2 == std::string::npos) {
        send_packet("E01");
        return;
    }

    int bp_type = std::stoi(args.substr(1, comma1 - 1));
    uint32_t address = std::stoul(args.substr(comma1 + 1, comma2 - comma1 - 1), nullptr, 16);

    if (bp_type != 0) {  // Only software breakpoints supported
        send_packet("");
        return;
    }

    bool success;
    if (type == 'Z') {
        success = DEBUG_SetBreakpoint(address);
    } else {
        success = DEBUG_RemoveBreakpoint(address);
    }

    send_packet(success ? "OK" : "E01");
}

void GDBServer::handle_query(const std::string& cmd) {
    if (cmd.substr(0, 10) == "Supported:") {
        send_packet("PacketSize=3fff;swbreak+;vContSupported+;QStartNoAckMode+");
    } else if (cmd.substr(0, 11) == "fThreadInfo") {
        send_packet("m1");
    } else if (cmd.substr(0, 11) == "sThreadInfo") {
        send_packet("l");
    } else if (cmd.substr(0, 8) == "Attached") {
        send_packet("1");
    } else {
        send_packet("");
    }
}

std::string GDBServer::hex_decode(const std::string& input) {
    std::string output;
    for (size_t i = 0; i + 1 < input.length(); i += 2) {
        uint8_t byte = (hex_to_int(input[i]) << 4) | hex_to_int(input[i + 1]);
        output.push_back(static_cast<char>(byte));
    }
    return output;
}

uint8_t GDBServer::hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

#endif /* C_REMOTEDEBUG */

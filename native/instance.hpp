#pragma once
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
namespace moader_native {
// Kernel-owned lifetime lock; a stale SD file cannot claim a living service.
// No traffic is sent. Only one library writer can bind this loopback endpoint.
class InstanceLock {
    int descriptor_ = -1;
public:
    InstanceLock() {
        descriptor_ = ::socket(AF_INET,SOCK_DGRAM,0);
        if (descriptor_ < 0) throw std::runtime_error("Could not initialize Moader's local instance lock.");
        sockaddr_in address{}; address.sin_family = AF_INET;
        address.sin_port = htons(7713); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(descriptor_,reinterpret_cast<sockaddr*>(&address),sizeof(address))) {
            const int error = errno;
            ::close(descriptor_); descriptor_ = -1;
            if (error == EADDRINUSE)
                throw std::runtime_error("Moader is already running. Reopen the app to connect, or close its foreground copy.");
            throw std::runtime_error(std::string("Could not bind Moader's local instance lock: ") + std::strerror(error));
        }
    }
    ~InstanceLock() { if (descriptor_ >= 0) ::close(descriptor_); }
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;
};
}

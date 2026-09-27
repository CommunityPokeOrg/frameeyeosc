// The debug gaze dots: the packet, the geometry and the socket.
#include "gaze_dots.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstring>

namespace gaze_dots {

namespace {

constexpr uint8_t kMagic[4] = {'F', 'E', 'O', 'D'};
constexpr uint8_t kVersion = 1;

// The Frame is little-endian (aarch64), like every machine this is built for; the packet is read with memcpy
static_assert(sizeof(float) == 4 && sizeof(double) == 8, "the packet has 4-byte floats and an 8-byte double");

}  // namespace

bool decode(const uint8_t* data, size_t size, Packet& out) {
    if (size != kPacketSize || std::memcmp(data, kMagic, 4) != 0 || data[4] != kVersion) return false;
    Packet packet;
    packet.independent = (data[5] & 1) != 0;
    std::memcpy(&packet.time, data + 8, 8);
    std::memcpy(packet.gaze, data + 16, 24);
    for (float value : packet.gaze) {
        if (!std::isfinite(value)) return false;
    }
    out = packet;
    return true;
}

std::vector<uint8_t> encode(const Packet& packet) {
    std::vector<uint8_t> data(kPacketSize, 0);
    std::memcpy(data.data(), kMagic, 4);
    data[4] = kVersion;
    data[5] = packet.independent ? 1 : 0;
    std::memcpy(data.data() + 8, &packet.time, 8);
    std::memcpy(data.data() + 16, packet.gaze, 24);
    return data;
}

Pose dotPose(double x, double y, int eye, double ipd) {
    const double scale = 45.0 * M_PI / 180.0;
    // The ray the tracker's angles describe, from the eye
    const double rx = std::tan(x * scale);
    const double ry = std::tan(y * scale);
    const double length = std::sqrt(rx * rx + ry * ry + 1.0);
    Pose pose;
    pose.position.x = eye * ipd / 2 + kDistanceM * rx / length;
    pose.position.y = kDistanceM * ry / length;
    pose.position.z = -kDistanceM / length;
    // Facing back along the ray: turned right by its yaw, up by its pitch
    pose.yawDeg = std::atan2(rx, 1.0) * 180.0 / M_PI;
    pose.pitchDeg = std::asin(ry / length) * 180.0 / M_PI;
    return pose;
}

Receiver::~Receiver() {
    close();
}

bool Receiver::open(const std::string& path, std::string& error) {
    close();
    sockaddr_un address {};
    if (path.size() >= sizeof(address.sun_path)) {
        error = "socket path too long: " + path;
        return false;
    }
    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    // Only this panel binds it (single instance), so an existing file is left over from a crash
    ::unlink(path.c_str());
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        error = "bind " + path + ": " + std::strerror(errno);
        ::close(fd);
        return false;
    }
    fd_ = fd;
    path_ = path;
    return true;
}

void Receiver::close() {
    if (fd_ < 0) return;
    ::close(fd_);
    ::unlink(path_.c_str());
    fd_ = -1;
}

bool Receiver::poll(Packet& latest) {
    if (fd_ < 0) return false;
    bool got = false;
    uint8_t buffer[64];
    for (;;) {
        const ssize_t n = ::recv(fd_, buffer, sizeof(buffer), 0);
        if (n < 0) break;  // EAGAIN: nothing more waiting
        Packet packet;
        if (decode(buffer, static_cast<size_t>(n), packet)) {
            latest = packet;
            got = true;
        }
    }
    return got;
}

}  // namespace gaze_dots

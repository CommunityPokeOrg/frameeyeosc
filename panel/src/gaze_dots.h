// The debug gaze dots: frameeyeosc streams the gaze it sends (one datagram per sample, see src/dots.rs) to a Unix
// socket in the status folder while "gaze_debug_dots" is on, and the panel shows a dot 2 m ahead where that gaze
// points: one for the combined gaze, or one per eye, each along that eye's own ray. Nothing here talks to OpenVR,
// so the packet and the geometry are tested on their own (gaze_dots_test.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gaze_dots {

/** The socket's name in the status folder (the same as frameeyeosc's). */
constexpr const char* kSocketName = "gaze-dots.sock";
/** One datagram: b"FEOD", version 1, flags, 2 zero bytes, time (f64), six gaze values (f32), little-endian. */
constexpr size_t kPacketSize = 40;
/** How far ahead the dots are shown (m). */
constexpr double kDistanceM = 2.0;
/** Without a packet for this long, the dots are hidden (frameeyeosc stopped or tracking is lost). */
constexpr double kStaleSec = 1.0;

/** One sample as frameeyeosc sent it. */
struct Packet {
    double time = 0.0;
    float gaze[6] = {0, 0, 0, 0, 0, 0};  ///< left x/y, right x/y, combined x/y; 1.0 = 45°
    bool independent = false;           ///< each eye's own gaze is sent (two dots)
};

/**
 * Read a datagram.
 * @param data the bytes
 * @param size how many
 * @param out the sample
 * @return false if it is not a gaze dots packet of this version
 */
bool decode(const uint8_t* data, size_t size, Packet& out);

/**
 * Write a datagram (the other direction, for tests).
 * @param packet the sample
 * @return the bytes
 */
std::vector<uint8_t> encode(const Packet& packet);

/** A point in head space: +x right, +y up, -z ahead (m). */
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/** Where a dot goes and which way it faces. */
struct Pose {
    Vec3 position;
    double yawDeg = 0.0;    ///< turned this far right...
    double pitchDeg = 0.0;  ///< ...and up, facing back along the ray
};

/**
 * Where a gaze points, kDistanceM along the ray from an eye. The gaze values are the tracker's angles
 * (atan2(x, -z) and atan2(y, -z), 1.0 = 45°), so the ray is (tan x, tan y, -1).
 * @param x sideways gaze (1.0 = 45° right)
 * @param y up/down gaze (1.0 = 45° up)
 * @param eye -1 = the left eye (at -ipd/2), 0 = between the eyes, 1 = the right eye (at +ipd/2)
 * @param ipd the distance between the eyes (m)
 * @return the pose
 */
Pose dotPose(double x, double y, int eye, double ipd);

/** The panel's end of the socket: bound while the dots are on, non-blocking. */
class Receiver {
public:
    Receiver() = default;
    ~Receiver();
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    /**
     * Bind the socket (a stale one left by a crash is replaced).
     * @param path the socket path
     * @param error why it failed
     * @return true if listening
     */
    bool open(const std::string& path, std::string& error);

    /** Stop listening and remove the socket file. */
    void close();

    /** @return true while listening */
    bool isOpen() const { return fd_ >= 0; }

    /**
     * Read everything waiting and keep the newest.
     * @param latest the newest sample (unchanged if none)
     * @return true if there was one
     */
    bool poll(Packet& latest);

private:
    int fd_ = -1;
    std::string path_;
};

}  // namespace gaze_dots

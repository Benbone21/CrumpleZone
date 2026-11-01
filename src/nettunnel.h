// A reliable, in-order byte stream over UDP datagrams: what a guest that joined with a code uses
// in place of the TCP connection (docs/plans/join-codes.md). Numbered segments, a cumulative ack
// with a 64-segment selective-ack bitmap, retransmission on a measured timeout, and a small AIMD
// window. No sockets here: the caller moves the packets (direct, or through the relay), so tests
// can lose, delay and reorder them.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace net {

class ReliableStream {
public:
    static constexpr size_t kSegment = 1100;     // bytes of the stream a segment carries
    static constexpr int kMaxWindow = 256;       // segments in flight at most (~280 KB)
    static constexpr int kMaxTries = 14;         // sends of one segment before the stream gives up
    static constexpr size_t kMaxQueued = 24u << 20;  // bytes waiting to go (a peer that can't keep up is dropped)

    // What goes on the wire, for the caller to frame and send. DATA: `seq` and bytes; ACK: the
    // next segment wanted and which of the 64 after it have come.
    struct Out {
        bool ack;
        uint32_t seq;
        uint64_t bits;
        const uint8_t* p;
        size_t n;
    };
    using Send = std::function<void(const Out&)>;

    // More bytes to send. False if that's more than kMaxQueued waiting (the stream is then broken).
    bool write(const uint8_t* p, size_t n);
    // A DATA segment arrived.
    void onData(uint32_t seq, const uint8_t* p, size_t n, double now);
    // An ACK arrived.
    void onAck(uint32_t next, uint64_t bits, double now);
    // Every frame (and after packets came in): sends what the window allows, resends what's overdue,
    // acks what came, a keepalive ack once a second.
    void service(double now, const Send& send);
    // What has arrived in order, for the reader to take (and clear).
    std::vector<uint8_t> rx;

    bool broken = false;        // a segment went unacked kMaxTries times, or too much queued
    double lastHeard = -1;      // when the other side last sent anything (data or ack)
    double srtt = 0.2, rttvar = 0.1, rto = 0.6;
    int cwnd = 16;
    size_t queued() const { return pendingBytes() + inFlightBytes; }
    uint64_t resent = 0, segmentsSent = 0;

private:
    struct Seg {
        uint32_t seq;
        std::vector<uint8_t> data;
        double sentAt = -1;
        int tries = 0;
        bool sacked = false;
    };
    std::deque<Seg> flight;              // sent, not acked (in order)
    std::deque<std::vector<uint8_t>> waiting;  // whole segments not sent yet
    std::vector<uint8_t> partial;        // the last segment being filled
    size_t waitingBytes = 0, inFlightBytes = 0;
    size_t pendingBytes() const { return waitingBytes + partial.size(); }
    uint32_t nextSeq = 1, rcvNext = 1;
    std::map<uint32_t, std::vector<uint8_t>> early;  // came ahead of a gap
    bool ackDue = false;
    double lastSend = -1, lastLoss = -1;
    double growth = 0;
    uint64_t sackBits() const;
};

}  // namespace net

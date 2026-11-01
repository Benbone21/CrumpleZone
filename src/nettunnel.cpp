// A reliable byte stream over datagrams: see nettunnel.h.
#include "nettunnel.h"

#include <algorithm>
#include <cmath>

namespace net {

bool ReliableStream::write(const uint8_t* p, size_t n) {
    if (broken) return false;
    if (queued() + n > kMaxQueued) {
        broken = true;
        return false;
    }
    partial.insert(partial.end(), p, p + n);
    return true;
}

uint64_t ReliableStream::sackBits() const {
    uint64_t b = 0;
    for (auto it = early.upper_bound(rcvNext); it != early.end() && it->first <= rcvNext + 64; ++it) b |= 1ull << (it->first - rcvNext - 1);
    return b;
}

void ReliableStream::onData(uint32_t seq, const uint8_t* p, size_t n, double now) {
    lastHeard = now;
    ackDue = true;
    if (seq < rcvNext || n > kSegment) return;  // (an old copy: the ack says so again)
    if (seq == rcvNext) {
        rx.insert(rx.end(), p, p + n);
        rcvNext++;
        for (auto it = early.find(rcvNext); it != early.end(); it = early.find(rcvNext)) {
            rx.insert(rx.end(), it->second.begin(), it->second.end());
            early.erase(it);
            rcvNext++;
        }
        return;
    }
    if (seq - rcvNext <= (uint32_t)kMaxWindow * 2 && early.size() < (size_t)kMaxWindow * 2) early.emplace(seq, std::vector<uint8_t>(p, p + n));
}

void ReliableStream::onAck(uint32_t next, uint64_t bits, double now) {
    lastHeard = now;
    // The round trip, from segments sent once (Karn) that this ack is the first to say have come
    // (one held back behind a hole, sacked long ago, would say the line is slow when it isn't).
    auto sample = [&](const Seg& s) {
        if (s.tries != 1 || s.sacked || s.sentAt < 0) return;
        const double rtt = std::max(0.0, now - s.sentAt);
        rttvar = 0.75 * rttvar + 0.25 * std::fabs(srtt - rtt);
        srtt = 0.875 * srtt + 0.125 * rtt;
        rto = std::min(3.0, std::max(0.2, srtt + 4 * rttvar));
    };
    while (!flight.empty() && flight.front().seq < next) {
        const Seg& s = flight.front();
        sample(s);
        inFlightBytes -= s.data.size();
        flight.pop_front();
        growth += lastLoss < 0 && cwnd < 64 ? 1.0 : 1.0 / cwnd;  // (doubling each round trip until the first loss, then one a round trip)
        while (growth >= 1) growth -= 1, cwnd = std::min(kMaxWindow, cwnd + 1);
    }
    for (Seg& s : flight)
        if (s.seq > next && s.seq <= next + 64 && (bits >> (s.seq - next - 1) & 1) && !s.sacked) sample(s), s.sacked = true;
}

void ReliableStream::service(double now, const Send& send) {
    if (broken) return;
    while (!partial.empty()) {
        const size_t k = std::min(partial.size(), kSegment);
        waiting.emplace_back(partial.begin(), partial.begin() + k);
        waitingBytes += k;
        partial.erase(partial.begin(), partial.begin() + k);
    }
    auto loss = [&] {
        if (now - lastLoss > srtt) cwnd = std::max(8, cwnd * 7 / 10), lastLoss = now;
    };
    // Overdue ones again (each waits twice as long as the last time), and a hole with three
    // acked after it at once.
    int resends = 0, sackedAfter = 0;
    for (auto it = flight.rbegin(); it != flight.rend() && resends < cwnd; ++it) {
        Seg& s = *it;
        if (s.sacked) {
            sackedAfter++;
            continue;
        }
        const double due = s.sentAt + rto * (double)(1 << std::min(s.tries - 1, 4));
        const bool hole = sackedAfter >= 4 && now - s.sentAt > srtt * 1.25 + 2 * rttvar;  // (not just late behind a reordering)
        if (now < due && !hole) continue;
        if (s.tries >= kMaxTries) {
            broken = true;
            return;
        }
        s.tries++;
        s.sentAt = now;
        resent++;
        resends++;
        loss();
        send({false, s.seq, 0, s.data.data(), s.data.size()});
        lastSend = now;
    }
    // New ones, as far as the window goes (segments the other side has, past a hole, don't count:
    // a lost one being resent doesn't hold up the rest).
    int pipe = 0;
    for (const Seg& s : flight) pipe += !s.sacked;
    while (pipe < cwnd && (int)flight.size() < 2 * kMaxWindow && !waiting.empty()) {
        pipe++;
        Seg s;
        s.seq = nextSeq++;
        s.data = std::move(waiting.front());
        waiting.pop_front();
        waitingBytes -= s.data.size();
        inFlightBytes += s.data.size();
        s.sentAt = now;
        s.tries = 1;
        segmentsSent++;
        flight.push_back(std::move(s));
        const Seg& f = flight.back();
        send({false, f.seq, 0, f.data.data(), f.data.size()});
        lastSend = now;
    }
    if (ackDue || lastSend < 0 || now - lastSend >= 1.0) {  // (the ack doubles as the keepalive)
        send({true, rcvNext, sackBits(), nullptr, 0});
        ackDue = false;
        lastSend = now;
    }
}

}  // namespace net

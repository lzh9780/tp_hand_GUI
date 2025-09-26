#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
#include "zcan.h"          // vendor header

namespace grip {

struct RxMessage {
    uint32_t id{};
    uint32_t timestamp{};
    std::vector<uint8_t> data;
};

struct BitTime {
    int tseg1{}, tseg2{}, sjw{}, brp{};
};

class CanBus {
public:
    explicit CanBus(uint32_t dev_type, uint32_t dev_index);

    void open();
    void configure(uint32_t channel, uint32_t core_clock_hz,
                   const BitTime& arbitration, const BitTime& data_phase,
                   bool termination_enabled);
    void start(uint32_t channel);
    void close();

    static ZCAN_FD_MSG buildFD(uint32_t id, const std::vector<uint8_t>& payload,
                               uint8_t channel = 0, uint16_t delay_ms = 0);

    uint32_t transmitFD(uint32_t channel, const std::vector<ZCAN_FD_MSG>& frames);
    std::vector<RxMessage> receiveFD(uint32_t channel, uint32_t max_frames, uint32_t wait_ms);

private:
    uint32_t dev_type_;
    uint32_t dev_index_;
};

} // namespace grip

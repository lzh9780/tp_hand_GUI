#include "grip/can_bus.hpp"
#include <cstring>

namespace grip
{

    CanBus::CanBus(uint32_t dev_type, uint32_t dev_index)
        : dev_type_(dev_type), dev_index_(dev_index) {}

    void CanBus::open()
    {
        if (!VCI_OpenDevice(dev_type_, dev_index_, 0))
            throw std::runtime_error("VCI_OpenDevice failed");
    }

    void CanBus::configure(uint32_t channel, uint32_t core_clock_hz,
                           const BitTime &arbitration, const BitTime &data_phase,
                           bool termination_enabled)
    {
        ZCAN_INIT cfg{};
        cfg.clk = core_clock_hz;
        cfg.mode = 0;
        cfg.aset.tseg1 = (uint8_t)arbitration.tseg1;
        cfg.aset.tseg2 = (uint8_t)arbitration.tseg2;
        cfg.aset.sjw = (uint8_t)arbitration.sjw;
        cfg.aset.smp = 0;
        cfg.aset.brp = (uint16_t)arbitration.brp;
        cfg.dset.tseg1 = (uint8_t)data_phase.tseg1;
        cfg.dset.tseg2 = (uint8_t)data_phase.tseg2;
        cfg.dset.sjw = (uint8_t)data_phase.sjw;
        cfg.dset.smp = 0;
        cfg.dset.brp = (uint16_t)data_phase.brp;
        if (!VCI_InitCAN(dev_type_, dev_index_, channel, &cfg))
            throw std::runtime_error("VCI_InitCAN failed");
        if (termination_enabled)
        {
            uint32_t on = 1;
            VCI_SetReference(dev_type_, dev_index_, channel, CMD_CAN_TRES, &on);
        }
    }

    void CanBus::start(uint32_t channel)
    {
        if (!VCI_StartCAN(dev_type_, dev_index_, channel))
            throw std::runtime_error("VCI_StartCAN failed");
    }
    void CanBus::close()
    {
        if (!VCI_CloseDevice(dev_type_, dev_index_))
            throw std::runtime_error("VCI_CloseDevice failed");
    }

    ZCAN_FD_MSG CanBus::buildFD(uint32_t id, const std::vector<uint8_t> &payload, uint8_t ch, uint16_t delay_ms)
    {
        if (payload.size() > 64)
            throw std::invalid_argument("payload too long");
        ZCAN_FD_MSG m{};
        m.hdr.inf.txm = 0;
        m.hdr.inf.fmt = 1;
        m.hdr.inf.sdf = 0;
        m.hdr.inf.sef = 1;
        m.hdr.inf.brs = 1;
        m.hdr.id = id;
        m.hdr.chn = ch;
        m.hdr.len = (uint8_t)payload.size();
        if (delay_ms)
        {
            m.hdr.pad = delay_ms;
            m.hdr.inf.qsend = 1;
            m.hdr.inf.qsend_100us = 0;
        }
        std::memcpy(m.dat, payload.data(), payload.size());
        return m;
    }

    uint32_t CanBus::transmitFD(uint32_t ch, const std::vector<ZCAN_FD_MSG> &frames)
    {
        if (frames.empty())
            return 0;
        return VCI_TransmitFD(dev_type_, dev_index_, ch, const_cast<ZCAN_FD_MSG *>(frames.data()), (uint32_t)frames.size());
    }

    std::vector<RxMessage> CanBus::receiveFD(uint32_t ch, uint32_t max_frames, uint32_t wait_ms)
    {
        std::vector<RxMessage> out;
        std::vector<ZCAN_FD_MSG> buf(max_frames);
        uint32_t n = VCI_ReceiveFD(dev_type_, dev_index_, ch, buf.data(), max_frames, wait_ms);
        out.reserve(n);
        for (uint32_t i = 0; i < n; ++i)
        {
            RxMessage r;
            r.id = buf[i].hdr.id;
            r.timestamp = buf[i].hdr.ts;
            r.data.assign(buf[i].dat, buf[i].dat + buf[i].hdr.len);
            out.emplace_back(std::move(r));
        }
        return out;
    }

} // namespace grip

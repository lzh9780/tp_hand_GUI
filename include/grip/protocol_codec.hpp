#pragma once
#include <cstdint>
#include <vector>
#include "can_defs.hpp"

namespace grip
{

    inline uint32_t make_can_id(JointDevice dev, MsgKind kind,
                                HandSide side = HandSide::LEFT,
                                Direction dir = Direction::FORWARD)
    {
        return (uint32_t)dir | (uint32_t)side | (uint32_t)dev | (uint32_t)kind;
    }

    inline void put_i32(std::vector<uint8_t> &out, int32_t v)
    {
        out.push_back(uint8_t(v & 0xFF));
        out.push_back(uint8_t((v >> 8) & 0xFF));
        out.push_back(uint8_t((v >> 16) & 0xFF));
        out.push_back(uint8_t((v >> 24) & 0xFF));
    }

    inline void put_i16(std::vector<uint8_t> &out, int32_t v)
    {
        out.push_back(uint8_t(v & 0xFF));
        out.push_back(uint8_t((v >> 8) & 0xFF));
    }

    inline int16_t rd_i16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
    inline uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    inline int32_t rd_i32(const uint8_t *p) { return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }

    inline std::vector<uint8_t> pack_broadcast_payload(
        const std::vector<JointDevice> &devices, uint8_t field, const std::vector<int32_t> &values)
    {
        std::vector<uint8_t> buf;
        buf.reserve(devices.size() * 6);
        for (size_t i = 0; i < devices.size(); ++i)
        {
            buf.push_back(uint8_t((uint32_t)devices[i] & 0xFF));
            buf.push_back(field);
            put_i32(buf, values[i]);
        }
        return buf;
    }

    inline std::vector<uint8_t> pack_broadcast_payload(
        const std::vector<JointDevice> &devices, uint8_t field, const std::vector<int16_t> &values1, const std::vector<int16_t> &values2)
    {
        std::vector<uint8_t> buf;
        buf.reserve(devices.size() * 6);
        for (size_t i = 0; i < devices.size(); ++i)
        {
            buf.push_back(uint8_t((uint32_t)devices[i] & 0xFF));
            buf.push_back(field);
            put_i16(buf, values1[i]);
            put_i16(buf, values2[i]);
        }
        return buf;
    }

    inline std::vector<uint8_t> pack_single_payload(JointDevice dev, uint8_t field, int32_t value)
    {
        std::vector<uint8_t> buf;
        buf.reserve(6);
        buf.push_back(uint8_t((uint32_t)dev & 0xFF));
        buf.push_back(field);
        put_i32(buf, value);
        return buf;
    }

    inline std::vector<uint8_t> pack_single_payload(JointDevice dev, uint8_t field, int16_t value1, int16_t value2)
    {
        std::vector<uint8_t> buf;
        buf.reserve(6);
        buf.push_back(uint8_t((uint32_t)dev & 0xFF));
        buf.push_back(field);
        put_i16(buf, value1);
        put_i16(buf, value2);
        return buf;
    }

} // namespace grip

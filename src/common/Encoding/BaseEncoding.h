/*
 * Copyright (C) 2016+  AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 * Copyright (C) 2021+  WarheadCore <https://github.com/WarheadCore>
 */

#ifndef WARHEAD_BASE_ENCODING_HPP
#define WARHEAD_BASE_ENCODING_HPP

#include "Define.h"
#include "Optional.h"
#include <numeric>
#include <string>
#include <vector>

namespace Acore::Impl
{
    template <typename Encoding>
    struct GenericBaseEncoding
    {
        static constexpr std::size_t BITS_PER_CHAR = Encoding::BITS_PER_CHAR;
        static constexpr std::size_t PAD_TO = std::lcm(8u, BITS_PER_CHAR);

        static_assert(BITS_PER_CHAR < 8, "Encoding parameters are invalid");

        static constexpr uint8 DECODE_ERROR = Encoding::DECODE_ERROR;
        static constexpr char PADDING = Encoding::PADDING;

        static constexpr std::size_t EncodedSize(std::size_t size)
        {
            size *= 8; // bits in input
            if (size % PAD_TO) // pad to boundary
                size += (PAD_TO - (size % PAD_TO));
            return (size / BITS_PER_CHAR);
        }

        static constexpr std::size_t DecodedSize(std::size_t size)
        {
            size *= BITS_PER_CHAR; // bits in input
            if (size % PAD_TO) // pad to boundary
                size += (PAD_TO - (size % PAD_TO));
            return (size / 8);
        }

        static std::string Encode(std::vector<uint8> const& data)
        {
            auto it = data.begin(), end = data.end();
            if (it == end)
                return "";

            std::string s;
            s.reserve(EncodedSize(data.size()));

            uint8 bitsLeft = 8; // in current byte
            do
            {
                uint8 thisC = 0;
                if (bitsLeft >= BITS_PER_CHAR)
                {
                    bitsLeft -= BITS_PER_CHAR;
                    thisC = ((*it >> bitsLeft) & ((1 << BITS_PER_CHAR)-1));
                    if (!bitsLeft)
                    {
                        ++it;
                        bitsLeft = 8;
                    }
                }
                else
                {
                    thisC = (*it & ((1 << bitsLeft) - 1)) << (BITS_PER_CHAR - bitsLeft);
                    bitsLeft += (8 - BITS_PER_CHAR);
                    if ((++it) != end)
                        thisC |= (*it >> bitsLeft);
                }
                s.append(1, Encoding::Encode(thisC));
            } while (it != end);

            while (bitsLeft != 8)
            {
                if (bitsLeft > BITS_PER_CHAR)
                    bitsLeft -= BITS_PER_CHAR;
                else
                    bitsLeft += (8 - BITS_PER_CHAR);
                s.append(1, PADDING);
            }

            return s;
        }

        static Optional<std::vector<uint8>> Decode(std::string const& data)
        {
            auto it = data.begin(), end = data.end();
            if (it == end)
                return std::vector<uint8>();

            std::vector<uint8> v;
            v.reserve(DecodedSize(data.size()));

            uint8 currentByte = 0;
            uint8 bitsLeft = 8; // in current byte
            while ((it != end) && (*it != PADDING))
            {
                uint8 cur = Encoding::Decode(*(it++));
                if (cur == DECODE_ERROR)
                    return {};

                if (bitsLeft > BITS_PER_CHAR)
                {
                    bitsLeft -= BITS_PER_CHAR;
                    currentByte |= (cur << bitsLeft);
                }
                else
                {
                    bitsLeft = BITS_PER_CHAR - bitsLeft; // in encoded char
                    currentByte |= (cur >> bitsLeft);
                    v.push_back(currentByte);
                    currentByte = (cur & ((1 << bitsLeft) - 1));
                    bitsLeft = 8 - bitsLeft; // in byte again
                    currentByte <<= bitsLeft;
                }
            }

            if (currentByte)
                return {}; // decode error, trailing non-zero bits

            // process padding
            while ((it != end) && (*it == PADDING) && (bitsLeft != 8))
            {
                if (bitsLeft > BITS_PER_CHAR)
                    bitsLeft -= BITS_PER_CHAR;
                else
                    bitsLeft += (8 - BITS_PER_CHAR);
                ++it;
            }

            // ok, all padding should be consumed, and we should be at end of string
            if (it == end)
                return v;

            // anything else is an error
            return {};
        }
    };
}

#endif

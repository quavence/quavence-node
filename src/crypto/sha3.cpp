// Copyright (c) 2020-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Based on https://github.com/mjosaarinen/tiny_sha3/blob/master/sha3.c
// by Markku-Juhani O. Saarinen <mjos@iki.fi>

#include "crypto/sha3.h"
#include "crypto/common.h"

#include <algorithm>
#include <cassert>
#include <string.h>

namespace {
static inline uint64_t rotl(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}
} // namespace

void KeccakF(uint64_t (&st)[25])
{
    static const uint64_t RNDC[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
        0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
        0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
        0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
    };
    static const int ROUNDS = 24;

    for (int round = 0; round < ROUNDS; ++round) {
        uint64_t bc0, bc1, bc2, bc3, bc4, t;

        // Theta
        bc0 = st[0] ^ st[5] ^ st[10] ^ st[15] ^ st[20];
        bc1 = st[1] ^ st[6] ^ st[11] ^ st[16] ^ st[21];
        bc2 = st[2] ^ st[7] ^ st[12] ^ st[17] ^ st[22];
        bc3 = st[3] ^ st[8] ^ st[13] ^ st[18] ^ st[23];
        bc4 = st[4] ^ st[9] ^ st[14] ^ st[19] ^ st[24];
        t = bc4 ^ rotl(bc1, 1); st[0] ^= t; st[5] ^= t; st[10] ^= t; st[15] ^= t; st[20] ^= t;
        t = bc0 ^ rotl(bc2, 1); st[1] ^= t; st[6] ^= t; st[11] ^= t; st[16] ^= t; st[21] ^= t;
        t = bc1 ^ rotl(bc3, 1); st[2] ^= t; st[7] ^= t; st[12] ^= t; st[17] ^= t; st[22] ^= t;
        t = bc2 ^ rotl(bc4, 1); st[3] ^= t; st[8] ^= t; st[13] ^= t; st[18] ^= t; st[23] ^= t;
        t = bc3 ^ rotl(bc0, 1); st[4] ^= t; st[9] ^= t; st[14] ^= t; st[19] ^= t; st[24] ^= t;

        // Rho Pi
        t = st[1];
        bc0 = st[10]; st[10] = rotl(t, 1); t = bc0;
        bc0 = st[7]; st[7] = rotl(t, 3); t = bc0;
        bc0 = st[11]; st[11] = rotl(t, 6); t = bc0;
        bc0 = st[17]; st[17] = rotl(t, 10); t = bc0;
        bc0 = st[18]; st[18] = rotl(t, 15); t = bc0;
        bc0 = st[3]; st[3] = rotl(t, 21); t = bc0;
        bc0 = st[5]; st[5] = rotl(t, 28); t = bc0;
        bc0 = st[16]; st[16] = rotl(t, 36); t = bc0;
        bc0 = st[8]; st[8] = rotl(t, 45); t = bc0;
        bc0 = st[21]; st[21] = rotl(t, 55); t = bc0;
        bc0 = st[24]; st[24] = rotl(t, 2); t = bc0;
        bc0 = st[4]; st[4] = rotl(t, 14); t = bc0;
        bc0 = st[15]; st[15] = rotl(t, 27); t = bc0;
        bc0 = st[23]; st[23] = rotl(t, 41); t = bc0;
        bc0 = st[19]; st[19] = rotl(t, 56); t = bc0;
        bc0 = st[13]; st[13] = rotl(t, 8); t = bc0;
        bc0 = st[12]; st[12] = rotl(t, 25); t = bc0;
        bc0 = st[2]; st[2] = rotl(t, 43); t = bc0;
        bc0 = st[20]; st[20] = rotl(t, 62); t = bc0;
        bc0 = st[14]; st[14] = rotl(t, 18); t = bc0;
        bc0 = st[22]; st[22] = rotl(t, 39); t = bc0;
        bc0 = st[9]; st[9] = rotl(t, 61); t = bc0;
        bc0 = st[6]; st[6] = rotl(t, 20); t = bc0;
        st[1] = rotl(t, 44);

        // Chi Iota
        bc0 = st[0]; bc1 = st[1]; bc2 = st[2]; bc3 = st[3]; bc4 = st[4];
        st[0] = bc0 ^ (~bc1 & bc2) ^ RNDC[round];
        st[1] = bc1 ^ (~bc2 & bc3);
        st[2] = bc2 ^ (~bc3 & bc4);
        st[3] = bc3 ^ (~bc4 & bc0);
        st[4] = bc4 ^ (~bc0 & bc1);
        bc0 = st[5]; bc1 = st[6]; bc2 = st[7]; bc3 = st[8]; bc4 = st[9];
        st[5] = bc0 ^ (~bc1 & bc2);
        st[6] = bc1 ^ (~bc2 & bc3);
        st[7] = bc2 ^ (~bc3 & bc4);
        st[8] = bc3 ^ (~bc4 & bc0);
        st[9] = bc4 ^ (~bc0 & bc1);
        bc0 = st[10]; bc1 = st[11]; bc2 = st[12]; bc3 = st[13]; bc4 = st[14];
        st[10] = bc0 ^ (~bc1 & bc2);
        st[11] = bc1 ^ (~bc2 & bc3);
        st[12] = bc2 ^ (~bc3 & bc4);
        st[13] = bc3 ^ (~bc4 & bc0);
        st[14] = bc4 ^ (~bc0 & bc1);
        bc0 = st[15]; bc1 = st[16]; bc2 = st[17]; bc3 = st[18]; bc4 = st[19];
        st[15] = bc0 ^ (~bc1 & bc2);
        st[16] = bc1 ^ (~bc2 & bc3);
        st[17] = bc2 ^ (~bc3 & bc4);
        st[18] = bc3 ^ (~bc4 & bc0);
        st[19] = bc4 ^ (~bc0 & bc1);
        bc0 = st[20]; bc1 = st[21]; bc2 = st[22]; bc3 = st[23]; bc4 = st[24];
        st[20] = bc0 ^ (~bc1 & bc2);
        st[21] = bc1 ^ (~bc2 & bc3);
        st[22] = bc2 ^ (~bc3 & bc4);
        st[23] = bc3 ^ (~bc4 & bc0);
        st[24] = bc4 ^ (~bc0 & bc1);
    }
}

SHA3_256::SHA3_256()
{
    Reset();
}

SHA3_256& SHA3_256::Write(const unsigned char* data, size_t len)
{
    if (m_bufsize && len >= sizeof(m_buffer) - m_bufsize) {
        // Fill the buffer and process it.
        std::copy(data, data + (sizeof(m_buffer) - m_bufsize), m_buffer + m_bufsize);
        data += sizeof(m_buffer) - m_bufsize;
        len -= sizeof(m_buffer) - m_bufsize;
        m_state[m_pos++] ^= ReadLE64(m_buffer);
        m_bufsize = 0;
        if (m_pos == RATE_BUFFERS) {
            KeccakF(m_state);
            m_pos = 0;
        }
    }
    while (len >= sizeof(m_buffer)) {
        // Process chunks directly from the buffer.
        m_state[m_pos++] ^= ReadLE64(data);
        data += 8;
        len -= 8;
        if (m_pos == RATE_BUFFERS) {
            KeccakF(m_state);
            m_pos = 0;
        }
    }
    if (len) {
        // Keep the remainder in the buffer.
        std::copy(data, data + len, m_buffer + m_bufsize);
        m_bufsize += len;
    }
    return *this;
}

SHA3_256& SHA3_256::Finalize(unsigned char output[OUTPUT_SIZE])
{
    std::fill(m_buffer + m_bufsize, m_buffer + sizeof(m_buffer), 0);
    m_buffer[m_bufsize] ^= 0x06;
    m_state[m_pos] ^= ReadLE64(m_buffer);
    m_state[RATE_BUFFERS - 1] ^= 0x8000000000000000ULL;
    KeccakF(m_state);
    for (unsigned i = 0; i < 4; ++i) {
        WriteLE64(output + 8 * i, m_state[i]);
    }
    return *this;
}

SHA3_256& SHA3_256::Reset()
{
    m_bufsize = 0;
    m_pos = 0;
    std::fill(m_state, m_state + 25, 0);
    return *this;
}

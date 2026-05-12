/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * See the file LICENSE for the full license text.
 */

#include "main.h"

#ifdef DH_PASSTHROUGH
#include "passthrough_uart.h"

/* Slot index per chunked packet type. Keep in sync with slot_for_type(). */
enum {
    REASM_SLOT_DESC = 0,
    REASM_SLOT_FRAME,
    REASM_SLOT_COUNT,
};

typedef struct {
    /* Reassembly buffer. Sized for the largest payload + length prefix +
       trailing pad up to the next 7-byte chunk boundary. */
    uint8_t  buf[PASSTHROUGH_UART_MAX_PAYLOAD + 2 + PASSTHROUGH_UART_CHUNK_DATA];
    uint16_t len;           /* bytes accumulated, including length prefix + pad */
    uint8_t  expected_seq;  /* next seq we expect to see */
    bool     in_progress;
    passthrough_uart_reasm_cb_t cb;
} reassembler_t;

static reassembler_t reassemblers[REASM_SLOT_COUNT];

static int slot_for_type(uint8_t packet_type) {
    switch (packet_type) {
        case TRACKPAD_DESC_CHUNK_MSG:  return REASM_SLOT_DESC;
        case TRACKPAD_FRAME_CHUNK_MSG: return REASM_SLOT_FRAME;
        default:                       return -1;
    }
}

bool passthrough_uart_register(uint8_t packet_type, passthrough_uart_reasm_cb_t cb) {
    int slot = slot_for_type(packet_type);
    if (slot < 0) return false;
    reassemblers[slot].cb = cb;
    return true;
}

bool passthrough_uart_send_chunked(uint8_t packet_type, const uint8_t *data, uint16_t len) {
    if (len == 0 || len > PASSTHROUGH_UART_MAX_PAYLOAD) return false;
    if (slot_for_type(packet_type) < 0) return false;

    /* Build the length-prefixed stream: [u16 len LE][payload][zero pad]. */
    uint16_t stream_len = 2 + len;
    uint8_t  num_chunks = (stream_len + PASSTHROUGH_UART_CHUNK_DATA - 1)
                          / PASSTHROUGH_UART_CHUNK_DATA;
    if (num_chunks > PASSTHROUGH_UART_MAX_CHUNKS) return false;

    /* Atomicity: reject the entire send if the TX queue can't absorb every
       chunk. Partial sends leave the receiver wedged in_progress=true with
       a half-assembled message until the next seq=0 lands, which (for the
       one-shot descriptor send) could be never. Refusing atomically lets
       the caller retry the whole message later. */
    uint32_t queue_used = queue_get_level(&global_state.uart_tx_queue);
    if (queue_used + num_chunks > UART_QUEUE_LENGTH) return false;

    for (uint8_t seq = 0; seq < num_chunks; seq++) {
        uint8_t chunk[PACKET_DATA_LENGTH] = {0};
        bool    is_last = (seq == num_chunks - 1);
        chunk[0] = seq | (is_last ? PASSTHROUGH_UART_LAST_FLAG : 0);

        /* Compute the offset of this chunk's 7 bytes within the stream and
           emit either the length prefix, payload bytes, or zero padding. */
        for (uint8_t i = 0; i < PASSTHROUGH_UART_CHUNK_DATA; i++) {
            uint16_t stream_offset = (uint16_t)seq * PASSTHROUGH_UART_CHUNK_DATA + i;
            if (stream_offset == 0) {
                chunk[1 + i] = (uint8_t)(len & 0xFF);
            } else if (stream_offset == 1) {
                chunk[1 + i] = (uint8_t)((len >> 8) & 0xFF);
            } else if (stream_offset < stream_len) {
                chunk[1 + i] = data[stream_offset - 2];
            }
            /* else: zero pad (chunk pre-initialized to 0) */
        }

        queue_packet(chunk, (enum packet_type_e)packet_type, PACKET_DATA_LENGTH);
    }
    return true;
}

void passthrough_uart_feed_chunk(uint8_t packet_type, const uint8_t *packet_data) {
    int slot = slot_for_type(packet_type);
    if (slot < 0) return;
    reassembler_t *r = &reassemblers[slot];

    uint8_t header  = packet_data[0];
    uint8_t seq     = header & PASSTHROUGH_UART_SEQ_MASK;
    bool    is_last = (header & PASSTHROUGH_UART_LAST_FLAG) != 0;

    /* seq=0 starts a new message; reset state. Lets the receiver recover
       from a previously-aborted (dropped chunk) reassembly without an
       out-of-band reset. */
    if (seq == 0) {
        r->len          = 0;
        r->expected_seq = 0;
        r->in_progress  = true;
    }

    /* Out-of-sequence: drop the in-progress message and wait for the next
       seq=0 to start fresh. */
    if (!r->in_progress || seq != r->expected_seq) {
        r->in_progress = false;
        return;
    }

    /* Overflow guard: should never trip given sender-side validation, but
       protects against malformed wire data. */
    if (r->len + PASSTHROUGH_UART_CHUNK_DATA > sizeof(r->buf)) {
        r->in_progress = false;
        return;
    }

    memcpy(&r->buf[r->len], &packet_data[1], PASSTHROUGH_UART_CHUNK_DATA);
    r->len += PASSTHROUGH_UART_CHUNK_DATA;
    r->expected_seq++;

    if (is_last) {
        /* Parse the 2-byte length prefix and validate against accumulated
           bytes. Reject if the declared payload exceeds what we received. */
        if (r->len >= 2) {
            uint16_t payload_len = (uint16_t)r->buf[0]
                                 | ((uint16_t)r->buf[1] << 8);
            if (payload_len <= PASSTHROUGH_UART_MAX_PAYLOAD
                && (uint16_t)(2 + payload_len) <= r->len) {
                if (r->cb) r->cb(&r->buf[2], payload_len);
            }
        }
        r->in_progress = false;
    }
}

#endif /* DH_PASSTHROUGH */

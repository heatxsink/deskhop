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
#pragma once

#ifdef DH_PASSTHROUGH

#include <stdbool.h>
#include <stdint.h>

/*==============================================================================
 *  Chunked UART transport for Magic Trackpad passthrough (Phase 2A plumbing)
 *
 *  deskhop's UART carries 8-byte payloads. Apple Magic Trackpad multi-touch
 *  frames are 22-67 bytes; the HID report descriptor is ~700 bytes. This
 *  module fragments large payloads into 8-byte UART packets and reassembles
 *  them on the other side.
 *
 *  Wire format per chunk (8-byte UART payload):
 *    byte 0:    header = [ last:1 | seq:7 ]
 *    bytes 1-7: 7 bytes of (length-prefixed payload + zero padding)
 *
 *  The reassembled byte stream is:
 *    bytes 0-1: u16 little-endian original payload length
 *    bytes 2..: payload bytes
 *    trailing: zero padding up to a 7-byte chunk boundary
 *
 *  Max original payload:  (128 chunks * 7 bytes) - 2 length bytes = 894 bytes.
 *  Sufficient for Apple's descriptor and any frame.
 *
 *  Sequencing: seq=0 starts a new message; subsequent chunks must arrive
 *  with monotonically increasing seq. Out-of-order or skipped chunks abort
 *  the in-progress reassembly for that message type. UART is in-order on a
 *  point-to-point link so this only fires on hardware drops.
 *
 *  Single in-flight message per type. The descriptor is one-shot at mount;
 *  frames are streamed serially. No interleaving.
 *============================================================================*/

#define PASSTHROUGH_UART_MAX_PAYLOAD  894
#define PASSTHROUGH_UART_CHUNK_DATA   7
#define PASSTHROUGH_UART_LAST_FLAG    0x80
#define PASSTHROUGH_UART_SEQ_MASK     0x7F
#define PASSTHROUGH_UART_MAX_CHUNKS   128

/* Invoked when a chunked message finishes reassembly. `data` and `len`
   reflect the original sender-side payload (length prefix already stripped,
   padding excluded). Buffer is owned by the reassembler -- copy if needed
   past the callback. */
typedef void (*passthrough_uart_reasm_cb_t)(const uint8_t *data, uint16_t len);

/* Register a reassembly callback for one of the chunked packet types
   (TRACKPAD_DESC_CHUNK_MSG, TRACKPAD_FRAME_CHUNK_MSG). Returns false if the
   type is unsupported. NULL callback clears the registration -- chunks will
   still reassemble but are silently dropped on completion. */
bool passthrough_uart_register(uint8_t packet_type, passthrough_uart_reasm_cb_t cb);

/* Split `data` into N UART chunks and enqueue them via queue_packet().
   Returns false if `len == 0` or `len > PASSTHROUGH_UART_MAX_PAYLOAD`.
   Caller is responsible for ensuring the TX queue can absorb up to
   ceil((len + 2) / 7) packets without overflow. */
bool passthrough_uart_send_chunked(uint8_t packet_type, const uint8_t *data, uint16_t len);

/* Feed a received 8-byte UART payload to the reassembler. Called from the
   dispatch handler in handlers.c. */
void passthrough_uart_feed_chunk(uint8_t packet_type, const uint8_t *packet_data);

#endif /* DH_PASSTHROUGH */

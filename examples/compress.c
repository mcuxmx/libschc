/*
 * (c) 2018 - idlab - UGent - imec
 *
 * Bart Moons
 *
 * This file is part of the SCHC stack implementation
 *
 * This is a basic example on how to compress 
 * and decompress a packet
 *
 */

#include <stdio.h>
#include <stdint.h>

#include "../schc.h"
#include "../compressor.h"

#define MAX_PACKET_LENGTH		128

// the ipv6/udp/coap packet
uint8_t msg[] = {
		// IPv6 header
		0x60, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x11, 0x40, 0xAB, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x02,
		// UDP header
		0x33, 0x16, 0x33, 0x16, 0x00, 0x1E, 0x27, 0x4E,
		// CoAP header
		0x54, 0x03, 0x23, 0xBB, 0x21, 0xFA, 0x01, 0xFB, 0xB5, 0x75, 0x73, 0x61, 0x67, 0x65, 0xD1, 0xEA, 0x1A, 0xFF,
		// Data
		0x01, 0x02, 0x03, 0x04
		/*
		0x60, 0x0, 0xf1, 0xcf, 0x0, 0x1e, 0x11, 0x40, 0x2a, 0x2, 0x18, 0x10, 0x2f, 0x1e, 0xe6, 0x0, 0xba, 0x27, 0xeb,
		0xff, 0xfe, 0x5d, 0x14, 0xa, 0x2a, 0x2, 0x18, 0x10, 0x2f, 0x1e, 0xe6, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
		0x2, 0x16, 0x33, 0x16, 0x33, 0x0, 0x1e, 0xf7, 0xc1, 0x64, 0x41, 0x89, 0xc4, 0xc4, 0x89, 0xa, 0x0, 0x82, 0x72,
		0x64, 0xa, 0x4d, 0x31, 0x77, 0x75, 0x4d, 0x6a, 0x4b, 0x47, 0x74, 0x31 */
};

int main() {
	// COMPRESSION
	// initialize the client compressor
	uint8_t src[16] = { 0xAA, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
	schc_compressor_init(src);
	
	uint8_t compressed_buf[MAX_PACKET_LENGTH] = { 0 };
	uint32_t device_id = 0x06;

	// compress packet
	struct schc_rule_t* schc_rule;
	schc_bitarray_t bit_arr;
	bit_arr.ptr = (uint8_t*) (compressed_buf);

	schc_rule = schc_compress(msg, sizeof(msg), &bit_arr, device_id, DOWN);

	// DECOMPRESSION
	uint8_t new_packet_len = 0;

	// NOTE: DIRECTION remains UP as this packet is forwarded to the IPv6 network
	unsigned char decomp_packet[MAX_PACKET_LENGTH] = { 0 };
	new_packet_len = schc_decompress(&bit_arr, decomp_packet, device_id,
			bit_arr.len, DOWN);
	if(new_packet_len == 0) { // some error occured
		return 1;
	}

 	return 0;
 }

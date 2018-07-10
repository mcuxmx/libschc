/*
 * (c) 2018 - idlab - UGent - imec
 *
 * Bart Moons
 *
 * This file is part of the SCHC stack implementation
 *
 */

#include <math.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "schc_config.h"

#include "fragmenter.h"

// keep track of the active connections
struct schc_fragmentation_t schc_rx_conns[SCHC_CONF_RX_CONNS];
static struct schc_fragmentation_t tx_connection;
static uint8_t fragmentation_buffer[MAX_MTU_LENGTH];

// ToDo
// create file bitm.c?
// function to add padding could live there too

/**
 * sets bits at a certain position in a bit array
 * big endian
 *
 * @param A				the bit array
 * @param pos			which bit to set
 * @param len			the number of consecutive bits to set
 *
 */
static void set_bits(uint8_t A[], uint32_t pos, uint32_t len) {
	uint32_t i;
	for(i = pos; i < (len + pos); i++) {
		A[i / 8] |= 128 >> (i % 8);
	}
}

/**
 * clear bits at a certain position in a bit array
 * big endian
 *
 * @param A				the bit array
 * @param pos			which bit to clear
 * @param len			the number of consecutive bits to clear
 *
 */
static void clear_bits(uint8_t A[], uint32_t pos, uint32_t len) {
	uint32_t i;
	for(i = pos; i < (len + pos); i++) {
		A[i / 8] &= ~(128 >> (i % 8));
	}
}

/**
 * copy bits to a certain position in a bit array
 * from another array
 * big endian
 *
 * @param DST			the array to copy to
 * @param dst_pos		which bit to start from
 * @param SRC			the array to copy from
 * @param src_pos		which bit to start from
 * @param len			the number of consecutive bits to set
 *
 */
static void copy_bits(uint8_t DST[], uint32_t dst_pos, uint8_t SRC[], uint32_t src_pos,
		uint32_t len) {
	uint32_t i;
	uint32_t k = 0;

	for(i = 0; i < len; i++) {
		uint8_t src_val = ((128 >> ( (k + src_pos) % 8)) & SRC[((k + src_pos) / 8)] );
		if(src_val) {
			set_bits(DST, i + dst_pos, 1);
		}
		k++;
	}
}

/**
 * compare two bit arrays
 *
 * @param 	DST			the array to compare
 * @param 	SRC			the array to compare with
 * @param 	len			the number of consecutive bits to compare
 *
 * @return	1			both arrays match
 * 			0			the arrays differ
 *
 */
static uint8_t compare_bits(uint8_t DST[], uint8_t SRC[], uint32_t len) {
	uint32_t i;

	for (i = 0; i < len; i++) {
		if ( (DST[i / 8] & (128 >> (i % 8) )) != (SRC[i / 8] & (128 >> (i % 8) )) ) {
			return 0;
		}
	}

	return 1;
}

/**
 * XOR two bit arrays
 *
 * @param 	DST			the array to save the result in
 * @param 	SRC1		the array to compare with
 * @param 	SRC2		the array to compare with
 * @param 	len			the number of consecutive bits to compare
 *
 */
static void xor_bits(uint8_t DST[], uint8_t SRC1[], uint8_t SRC2[], uint32_t len) {
	uint32_t i;

	for(i = 0; i < len; i++) {
		DST[i / 8] |= (SRC1[i / 8] & (128 >> (i % 8) )) ^ (SRC2[i / 8] & (128 >> (i % 8) ));
	}
}

/**
 * print a bitmap
 *
 * @param bitmap		the bit array
 * @param len			the number of consecutive bits to print
 *
 */
static void print_bitmap(uint8_t bitmap[], uint32_t length) {
	uint32_t i;
	for (i = 0; i < length; i++) {
		uint8_t bit = bitmap[i / 8] & 128 >> (i % 8);
		printf("%d ", bit ? 1 : 0);
	}
}

////////////////////////////////////////////////////////////////////////////////////
//                                LOCAL FUNCIONS                                  //
////////////////////////////////////////////////////////////////////////////////////

/**
 * Calculates the Message Integrity Check (MIC)
 * which is the 8- 16- or 32- bit Cyclic Redundancy Check (CRC)
 *
 * @param data 			pointer to the data packet
 *
 * @return checksum 	the computed checksum
 *
 */
static unsigned int compute_mic(schc_fragmentation_t *conn) {
	int i, j;
	unsigned int byte, crc, mask;

	// ToDo
	// check conn->mic length
	// and calculate appropriate crc

	i = 0;
	crc = 0xFFFFFFFF;

	uint16_t len = (conn->tail_ptr - conn->data_ptr);

	while (i < len) {
		byte = conn->data_ptr[i - 1];
		crc = crc ^ byte;
		for (j = 7; j >= 0; j--) {    // do eight times.
			mask = -(crc & 1);
			crc = (crc >> 1) ^ (0xEDB88320 & mask);
		}
		i++;
	}

	crc = ~crc;
	uint8_t mic[4] = { ((crc & 0xFF000000) >> 24), ((crc & 0xFF0000) >> 16),
			((crc & 0xFF00) >> 8), ((crc & 0xFF)) };

	memcpy((uint8_t *) conn->mic, mic, 4);

	DEBUG_PRINTF("compute_mic(): MIC for device %d is %02X%02X%02X%02X",
			conn->device_id, mic[0], mic[1], mic[2], mic[3]);


	return crc;
}

/**
 * find a connection based on a device id
 * or open a new connection if there was no connection
 * for this device yet
 *
 * @param 	device_id	the id of the device to open a connection for
 *
 * @return 	conn		a pointer to the selected connection
 * 			0 			if no free connections are available
 *
 */
static schc_fragmentation_t* get_connection(uint32_t device_id) {
	uint8_t i; schc_fragmentation_t *conn;
	conn = 0;

	for (i = 0; i < SCHC_CONF_RX_CONNS; i++) {
		// first look for the the old connection
		if (schc_rx_conns[i].device_id == device_id) {
			conn = &schc_rx_conns[i];
			break;
		}
	}

	if (conn == 0) { // check if we were given an old connection
		for (i = 0; i < SCHC_CONF_RX_CONNS; i++) {
			if (schc_rx_conns[i].device_id == 0) { // look for an empty connection
				conn = &schc_rx_conns[i];
				break;
			}
		}
	}

	if(conn) {
		DEBUG_PRINTF("get_connection(): selected connection %d for device %d", i, device_id);
	}

	return conn;
}

/**
 * set the fragmentation bit in the layered rule id
 * and return the rule id
 *
 * @param conn 			a pointer to the connection
 *
 * @return rule_id		a pointer to a buffer containing the rule id
 *
 */
static void set_fragmentation_bit(schc_fragmentation_t* conn) {
	// set fragmentation bit to 1
	set_bits(conn->rule_id, FRAG_POS, 1);
}

/**
 * initializes a new connection for a device:
 * set the starting and ending point of the packet
 * calculate the MIC over the complete SCHC packet
 *
 * @param conn 				a pointer to the connection to initialize
 *
 * @return	 1				on success
 * 			 0				on error
 * 			-1				if no fragmentation is needed
 *
 */
static int8_t init_connection(schc_fragmentation_t* conn) {
	if (!conn->data_ptr) {
		DEBUG_PRINTF(
				"init_connection(): no pointer to compressed packet given");
		return 0;
	}
	if (!conn->mtu) {
		DEBUG_PRINTF("init_connection(): no mtu specified");
		return 0;
	}
	if (conn->mtu > MAX_MTU_LENGTH) {
		DEBUG_PRINTF(
				"init_connection(): MAX_MTU_LENGTH should be set according to conn->mtu");
		return 0;
	}
	if (!conn->packet_len) {
		DEBUG_PRINTF("init_connection(): packet_length not specified");
		return 0;
	}
	if(conn->packet_len < conn->mtu) {
		DEBUG_PRINTF("init_connection(): no fragmentation needed");
		return -1;
	}
	if (conn->send == NULL) {
		DEBUG_PRINTF("init_connection(): no send function specified");
		return 0;
	}
	if (conn->post_timer_task == NULL) {
		DEBUG_PRINTF("init_connection(): no timer function specified");
		return 0;
	}

	memcpy(conn->rule_id, (uint8_t*) (conn->data_ptr + 0), RULE_SIZE_BYTES); // set rule id
	set_fragmentation_bit(conn); // set fragmentation bit in the rule id

	conn->tail_ptr = (uint8_t*) (conn->data_ptr + conn->packet_len); // set end of packet

	conn->window = 0;
	conn->window_cnt = 0;
	memset(conn->bitmap, 0, BITMAP_SIZE_BYTES); // clear bitmap
	conn->fcn = MAX_WIND_FCN;
	conn->frag_cnt = 0;
	conn->attempts = 0;

	if (conn->init_timer_task != NULL) {
		conn->init_timer_task(&schc_fragment);
	}

	compute_mic(conn); // calculate MIC over compressed, unfragmented packet

	return 1;
}

/**
 * reset a connection
 *
 * @param conn 			a pointer to the connection to reset
 *
 */
static void reset_connection(schc_fragmentation_t* conn) {
	/* reset connection variables */
	conn->mtu = 0;
	conn->fcn = 0;
	conn->data_ptr = 0;
	conn->tail_ptr = 0;
	conn->device_id = 0;
	conn->packet_len = 0;
	conn->dtag = 0;
	conn->window = 0;
	conn->window_cnt = 0;
	conn->dc = 0;
	conn->frag_cnt = 0;
	conn->attempts = 0;
	memset(conn->rule_id, 0, RULE_SIZE_BYTES);
	memset(conn->mic, 0, MIC_SIZE_BYTES);
	memset(conn->bitmap, 0, BITMAP_SIZE_BYTES);
	/* reset function callbacks */
	conn->send = NULL;
	conn->init_timer_task = NULL;
	conn->post_timer_task = NULL;
	conn->TX_STATE = INIT;
	/* reset ack structure */
	memset(conn->ack.bitmap, 0, BITMAP_SIZE_BYTES);
	memset(conn->ack.window, 0, 1);
	memset(conn->ack.dtag, 0, 1);
	conn->ack.mic = 0;
}

/**
 * check if a connection has more fragments to deliver
 *
 * @param conn 					a pointer to the connection
 *
 * @return	0					the connection still has fragments to send
 * 			total_bit_offset	the total bit offset inside the packet
 *
 */
static uint32_t has_no_more_fragments(schc_fragmentation_t* conn) {
	uint8_t total_fragments = ((conn->tail_ptr - conn->data_ptr) / conn->mtu);

	if (conn->frag_cnt > total_fragments) { // this is the last packet
		uint16_t bit_offset = RULE_SIZE_BITS + DTAG_SIZE_BITS + WINDOW_SIZE_BITS
				+ FCN_SIZE_BITS + (MIC_SIZE_BYTES * 8); // fragmentation header bits
		uint32_t total_bit_offset = ((conn->mtu * 8)
				- (RULE_SIZE_BITS + DTAG_SIZE_BITS + WINDOW_SIZE_BITS
						+ FCN_SIZE_BITS)) * (conn->frag_cnt - 1); // packet bit offset
		uint16_t total_byte_offset = total_bit_offset / 8;
		uint8_t remaining_bit_offset = total_bit_offset % 8;

		uint16_t packet_len = conn->tail_ptr - (conn->data_ptr
				+ total_byte_offset)
				+ (ceil((bit_offset + remaining_bit_offset) / 8));

		if (packet_len <= conn->mtu) { // if fragmentation header is small enough
			return total_bit_offset;
		}
	}

	return 0;
}

/**
 * set the fragmentation header
 *
 * @param conn 			a pointer to the connection
 * @param buffer		a pointer to the buffer to set the header
 *
 * @return bit_offset	the number of bits added to the front of the fragment
 *
 */
static uint16_t set_fragmentation_header(schc_fragmentation_t* conn,
		uint8_t* fragmentation_buffer) {
	uint8_t bit_offset = RULE_SIZE_BITS;

	 // set rule id
	copy_bits(fragmentation_buffer, 0, conn->rule_id, 0, bit_offset);

	// set dtag field
	uint8_t dtag[1] = { conn->dtag << (8 - DTAG_SIZE_BITS) };
	copy_bits(fragmentation_buffer, bit_offset, dtag, 0, DTAG_SIZE_BITS); // right after rule id

	bit_offset += DTAG_SIZE_BITS;

	// set window bit
	uint8_t window[1] = { conn->window << (8 - WINDOW_SIZE_BITS) };
	copy_bits(fragmentation_buffer, bit_offset, window, 0, 1); // right after dtag

	bit_offset += WINDOW_SIZE_BITS;

	// set fcn value
	uint8_t fcn[1] = { conn->fcn << (8 - FCN_SIZE_BITS) };
	copy_bits(fragmentation_buffer, bit_offset, fcn, 0, FCN_SIZE_BITS); // right after window bits

	bit_offset += FCN_SIZE_BITS;

	if (has_no_more_fragments(conn)) { // all-1 fragment
		// shift in MIC
		copy_bits(fragmentation_buffer, bit_offset, conn->mic, 0, (MIC_SIZE_BYTES * 8));
		bit_offset += (MIC_SIZE_BYTES * 8);
	}

	return bit_offset;
}

/**
 * sets the local bitmap, without encoding it
 *
 * @param conn 			a pointer to the connection
 *
 */
static void set_local_bitmap(schc_fragmentation_t* conn) {
	set_bits(conn->bitmap, conn->frag_cnt, 1); // set the bitmap at the current fragment offset
}

/**
 * clear the received and local bitmap
 *
 * @param conn 			a pointer to the connection
 *
 */
static void clear_bitmap(schc_fragmentation_t* conn) {
	memset(conn->bitmap, 0, BITMAP_SIZE_BYTES); // clear local bitmap
	memset(conn->ack.bitmap, 0, BITMAP_SIZE_BYTES); // clear received bitmap
}

/**
 * encode the bitmap by removing all the right
 * most contiguous BYTES in the non-encoded bitmap
 *
 * @param conn 			a pointer to the connection
 *
 */
static void encode_bitmap(schc_fragmentation_t* conn) {
	// ToDo
}

/**
 * reconstruct an encoded bitmap
 *
 * @param conn 			a pointer to the connection
 *
 */
static void decode_bitmap(schc_fragmentation_t* conn) {
	// ToDo
}

/**
 * get the next fragment to retransmit according the fragmentation counter
 *
 * @param conn 			a pointer to the connection
 *
 * @return  frag		the next fragment to retransmit
 * 			0			no more fragments to retransmit
 *
 */
static uint8_t get_next_fragment_from_bitmap(schc_fragmentation_t* conn) {
	uint32_t i;

	for (i = conn->frag_cnt; i <= MAX_WIND_FCN; i++) {
		uint8_t bit = conn->ack.bitmap[i / 8] & 128 >> (i % 8);
		if(bit) {
			return (i + 1);
		}
	}

	return 0;
}
/**
 * discard a fragment
 *
 * @param conn 			a pointer to the connection
 *
 */
static void discard_fragment() {
	return;
}

/**
 * sets the retransmission timer to re-enter the fragmentation loop
 *
 * @param conn 			a pointer to the connection
 *
 */
static void set_retrans_timer(schc_fragmentation_t* conn) {
	// conn->post_timer_task(&schc_fragment, conn->dc);
	DEBUG_PRINTF("set_retrans_timer(): for %d ms", conn->dc);
}

/**
 * stops the retransmission timer
 *
 * @param conn 			a pointer to the connection
 *
 */
static void stop_retrans_timer(schc_fragmentation_t* conn) {
	// conn->post_timer_task(&schc_fragment, conn->dc);
	DEBUG_PRINTF("stop_retrans_timer()");
}

/**
 * sets the duty cycle timer to re-enter the fragmentation loop
 *
 * @param conn 			a pointer to the connection
 *
 */
static void set_dc_timer(schc_fragmentation_t* conn) {
	// conn->post_timer_task(&schc_fragment, conn->dc);
	DEBUG_PRINTF("set_dc_timer(): for %d ms", conn->dc);
	schc_fragment();
}

static void send_fragment(schc_fragmentation_t* conn) {
	// set and reset buffer
	memset(fragmentation_buffer, 0, MAX_MTU_LENGTH);

	// set fragmentation header
	uint16_t bit_offset = set_fragmentation_header(conn, fragmentation_buffer);

	uint16_t packet_bit_offset = has_no_more_fragments(conn);
	uint16_t packet_len = 0; uint16_t total_byte_offset; uint8_t remaining_bit_offset;

	if(!packet_bit_offset) { // normal fragment
		packet_len = conn->mtu;
		packet_bit_offset = ((conn->mtu * 8) - bit_offset)
				* (conn->frag_cnt - 1); // the number of bits left to copy
	}

	total_byte_offset = packet_bit_offset / 8;
	remaining_bit_offset = packet_bit_offset % 8;

	if(!packet_len) { // all-1 fragment
		packet_len = conn->tail_ptr - (conn->data_ptr
				+ total_byte_offset)
				+ (ceil((bit_offset + remaining_bit_offset) / 8));
	}

	uint32_t packet_bits = ((packet_len * 8) - bit_offset);

	copy_bits(fragmentation_buffer, bit_offset,
			(conn->data_ptr + total_byte_offset),
			(remaining_bit_offset + RULE_SIZE_BITS), packet_bits); // copy bits

	DEBUG_PRINTF("send_fragment(): sending fragment %d with length %d",
			conn->frag_cnt, packet_len);
	log_print_data(fragmentation_buffer, packet_len);
}

////////////////////////////////////////////////////////////////////////////////////
//                               GLOBAL FUNCIONS                                  //
////////////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the SCHC fragmenter
 *
 * @param tx_conn		a pointer to the tx initialization structure
 *
 * @return error codes on error
 *
 */
int8_t schc_fragmenter_init(schc_fragmentation_t* tx_conn){
	uint8_t i;

	// initializes the schc tx connection
	reset_connection(tx_conn);

	// initializes the schc rx connections
	for (i = 0; i < SCHC_CONF_RX_CONNS; i++) {
		reset_connection(&schc_rx_conns[i]);
	}

	return 1;
}

int8_t schc_input(uint8_t* data, uint16_t len, schc_fragmentation_t* tx_conn,
		uint8_t device_id) {
	if (tx_conn->TX_STATE == WAIT_BITMAP
			&& compare_bits(tx_conn->rule_id, data, RULE_SIZE_BITS)) {
		uint8_t bit_offset = RULE_SIZE_BITS;

		copy_bits(tx_conn->ack.dtag, (8 - DTAG_SIZE_BITS), (uint8_t*) data,
				bit_offset, DTAG_SIZE_BITS); // get dtag
		bit_offset += DTAG_SIZE_BITS;

		copy_bits(tx_conn->ack.window, (8 - WINDOW_SIZE_BITS), (uint8_t*) data,
				bit_offset, WINDOW_SIZE_BITS); // get window
		bit_offset += WINDOW_SIZE_BITS;

		if(has_no_more_fragments(tx_conn)) { // ack contains mic check
			uint8_t mic[1] = { 0 };
			copy_bits(mic, 7, (uint8_t*) data, bit_offset, 1);
			bit_offset += 1;
			tx_conn->ack.mic = mic[0];
			if(tx_conn->ack.mic) { // end transmission
				tx_conn->TX_STATE = END;
				schc_fragment();
				return 0;
			} // else continue with bitmap decoding and retransmission
		}

		// ToDo
		// decode_bitmap(tx_conn);
		uint8_t fcn_bits = ((MAX_WIND_FCN / 8) + 1) * 8;
		copy_bits(tx_conn->ack.bitmap, 0, (uint8_t*) data, bit_offset,
				(MAX_WIND_FCN + 1));

		// copy bits to retransmit to intermediate buffer
		uint8_t resend_window[BITMAP_SIZE_BYTES] = { 0 };
		xor_bits(resend_window, tx_conn->bitmap, tx_conn->ack.bitmap,
				(MAX_WIND_FCN + 1));

		DEBUG_PRINTF(
				"FCN Bits = %d, Local bitmap: %02X %02X, received bitmap: %02X %02X, resend fragment %02X %02X",
				fcn_bits, tx_conn->bitmap[0], tx_conn->bitmap[1],
				tx_conn->ack.bitmap[0], tx_conn->ack.bitmap[1],
				resend_window[0], resend_window[1]);

		// copy retransmit window to ack.bitmap
		memset(tx_conn->ack.bitmap, 0, BITMAP_SIZE_BYTES);
		copy_bits(tx_conn->ack.bitmap, 0, resend_window, 0, (MAX_WIND_FCN + 1));

		schc_fragment();
	} else {
		schc_reassemble(data, len, device_id);
	}

	return 0;
}

// TODO
// REMOVE
// ARGUMENT IN SCHC_FRAGMENT
void set_tx_conn(schc_fragmentation_t* tx_conn) {
	memcpy(&tx_connection, tx_conn, sizeof(schc_fragmentation_t));
}

schc_fragmentation_t* get_tx_conn() {
	return &tx_connection;
}

/**
 * Fragments a compressed SCHC packet for a specified MTU
 * an open connection is picked for the device out of a pool of connections
 * to keep track of the original packet
 *
 * @param tx_conn		a pointer to the tx connection structure
 *
 * @return 	 0			TBD
 *        	-1			failed to initialize the connection
 *
 */
// TODO
// int8_t schc_fragment(schc_fragmentation_t* tx_conn) {
int8_t schc_fragment(){
	schc_fragmentation_t* tx_conn = get_tx_conn();

	switch(tx_conn->TX_STATE) {
	case INIT: {
		int8_t init = init_connection(tx_conn);
		if (!init) {
			return SCHC_FAILURE;
		} else if(init < 0) {
			return SCHC_NO_FRAGMENTATION;
		}
		tx_conn->TX_STATE = SEND;
		schc_fragment(tx_conn);
		break;
	}
	case SEND: {
		set_local_bitmap(tx_conn);
		tx_conn->frag_cnt++;

		if(has_no_more_fragments(tx_conn)) {
			DEBUG_PRINTF("schc_fragment(): all-1 window");
			tx_conn->fcn = (pow(2, FCN_SIZE_BITS) - 1); // all 1-window
			send_fragment(tx_conn);
			set_retrans_timer(tx_conn);
			tx_conn->TX_STATE = WAIT_BITMAP;
/*
			// TEST
			// MAX_FCN_WIND = 6
			// FCN_BITS = 3
			// uint8_t data[2] = { 0xA5, 0x78 }; // no missing fragments, MIC = 1
			uint8_t data[2] = { 0xA5, 0x20 }; // missing fragments 2 & 3, MIC = 0
			schc_input(data, 2, tx_conn, 1);
*/
		} else if(tx_conn->fcn == 0 && !has_no_more_fragments(tx_conn)) { // all-0 window
			DEBUG_PRINTF("schc_fragment(): all-0 window");
			send_fragment(tx_conn);
			tx_conn->fcn = MAX_WIND_FCN; // reset the FCN
			set_retrans_timer(tx_conn);
			tx_conn->TX_STATE = WAIT_BITMAP;

/*
			// TEST
			// MAX_FCN_WIND = 9
			// FCN_BITS = 4
			// WINDOW = 0
			if (tx_conn->window == 0) {
				uint8_t data[3] = { 0xA5, 0x5F, 0x80 }; // missing fragment 2, 9 & 10
				// uint8_t data[3] = { 0xA5, 0x7F, 0xE0 }; // no missing fragments
				schc_input(data, 3, tx_conn, 1);
			} else { // WINDOW = 1
				uint8_t data[2] = { 0xA5, 0xDF };
				schc_input(data, 2, tx_conn, 1);
			}
*/

		} else if(tx_conn->fcn != 0 && !has_no_more_fragments(tx_conn)) { // normal fragment
			DEBUG_PRINTF("schc_fragment(): normal fragment");
			send_fragment(tx_conn);
			tx_conn->fcn--;
			tx_conn->TX_STATE = SEND;
			set_dc_timer(tx_conn);
		}

		break;
	}
	case WAIT_BITMAP: {
		uint8_t resend_window[BITMAP_SIZE_BYTES] = { 0 }; // if ack.bitmap is all-0, there are no packets to retransmit
		if ((tx_conn->ack.window[0] == tx_conn->window)
				&& compare_bits(resend_window, tx_conn->ack.bitmap, (MAX_WIND_FCN + 1))) {
			if(!has_no_more_fragments(tx_conn)) { // no missing fragments & more fragments
				stop_retrans_timer(tx_conn);
				clear_bitmap(tx_conn);
				tx_conn->window = !tx_conn->window; // change window
				tx_conn->window_cnt++;
				tx_conn->TX_STATE = SEND;
				schc_fragment();
			} else if(has_no_more_fragments(tx_conn) && tx_conn->ack.mic) {

			}
		} else if(tx_conn->ack.window[0] != tx_conn->window) {
			// unexpected window
			discard_fragment();
			tx_conn->TX_STATE = WAIT_BITMAP;
		} else if (!compare_bits(resend_window, tx_conn->ack.bitmap,
				(MAX_WIND_FCN + 1))) { //ack.bitmap contains the missing fragments
			tx_conn->attempts++;
			tx_conn->frag_cnt = 0;
			tx_conn->TX_STATE = RESEND;
			schc_fragment();
		}
		break;
	}
	case RESEND: {
		// get the next fragment offset
		tx_conn->frag_cnt = get_next_fragment_from_bitmap(tx_conn); // send_fragment() uses frag_cnt to transmit a particular fragment
		if (!tx_conn->frag_cnt) { // no more missing fragments to send
			tx_conn->TX_STATE = WAIT_BITMAP;
			tx_conn->frag_cnt = (tx_conn->window_cnt + 1) * (MAX_WIND_FCN + 1);
			set_retrans_timer(tx_conn);
		} else {
			DEBUG_PRINTF("schc_fragment(): sending missing fragments for bitmap: ");
			print_bitmap(tx_conn->ack.bitmap, (MAX_WIND_FCN + 1));
			tx_conn->fcn = ((MAX_WIND_FCN + 1) * (tx_conn->window_cnt + 1))
					- tx_conn->frag_cnt;
			tx_conn->TX_STATE = RESEND;
			send_fragment(tx_conn); // retransmit the fragment
			set_dc_timer(tx_conn);
		}
		break;
	}
	case END: {
		DEBUG_PRINTF("schc_fragment(): end transmission cycle");
		stop_retrans_timer(tx_conn);
		// ToDo
		// stay alive to answer empty all-1 fragments, indicating lost ack(s)
		return SCHC_SUCCESS;
	}
	}


	return 0;
}

/**
 * Reassembles a fragmented SCHC packet
 * an open connection is picked for the device
 * out of a pool of connections to keep track of the packet
 *
 * @param data			a pointer to the mbuf
 * @param total_length	the total length of the compressed packet
 * @param device_id		the device it's ID, linked to it's connection
 *
 * @return 	 0			TBD
 *        	-1			no free connections were found
 *
 */
int8_t schc_reassemble(const uint8_t *data, uint16_t total_length,
		uint32_t device_id) {
	schc_fragmentation_t *conn;

	// get a connection for the device
	conn = get_connection(device_id);
	if (!conn) { // return if there was no connection available
		DEBUG_PRINTF("schc_fragment(): no free connections found!");
		return -1;
	}

	return 0;
}

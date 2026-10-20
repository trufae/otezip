/* ----------- Encoder-specific data structures ----------- */

/* Internal state for deflate */
typedef struct {
	/* Compression parameters */
	int level; /* Compression level */
	int is_last_block; /* Is this the final block? */
	int finalized; /* End-of-stream marker already emitted */

	/* Sliding window for LZ77 */
	uint8_t *window; /* Sliding window buffer */
	uint32_t window_size; /* Window size (power of 2) */
	uint32_t window_mask; /* Window mask (window_size - 1) */
	uint32_t pos; /* Stream position of the next input byte */

	/* Hash table for fast string matching */
	uint32_t *hash_table; /* Last stream position + 1 per hash, 0 = empty */
	uint32_t hash_size; /* Size of hash table */
	uint32_t hash_mask; /* Hash mask (hash_size - 1) */

	/* Output state */
	uint32_t bit_buffer; /* Bit buffer */
	uint32_t bits_in_buffer; /* Number of bits in buffer */

	/* Huffman tables */
	huffman_table literals; /* Literal/length codes */
	huffman_table distances; /* Distance codes */
} deflate_state;

/* ----------- Encoder-specific functions ----------- */

/* Find longest match for the input at data (stream position state->pos).
 * Returns the match length (0 if none) and stores its distance. */
static int find_longest_match(deflate_state *state, const uint8_t *data, uint32_t max_len, uint32_t *distance) {
	/* Need at least 3 bytes for a match */
	if (max_len < 3) {
		return 0;
	}
	uint32_t cur = state->pos;
	uint32_t hash = calculate_hash (data) & state->hash_mask;
	uint32_t stored = state->hash_table[hash];
	state->hash_table[hash] = cur + 1;
	if (stored == 0) {
		return 0;
	}
	uint32_t match = stored - 1;
	uint32_t dist = cur - match;
	/* Only the last window_size bytes are still in the window */
	if (dist == 0 || dist > state->window_size) {
		return 0;
	}
	/* Bytes before cur are in the window; an overlapping match (dist < len)
	 * continues into the input itself, exactly as the decoder copies it */
	uint32_t len = 0;
	while (len < max_len) {
		uint32_t src = match + len;
		uint8_t b = (len < dist)? state->window[src & state->window_mask]: data[len - dist];
		if (b != data[len]) {
			break;
		}
		len++;
	}
	if (len < 3) {
		return 0;
	}
	*distance = dist;
	return len;
}

/* Write bits to output buffer */
static int write_bits(z_stream *strm, deflate_state *state, uint32_t bits, int num_bits) {
	/* Add bits to buffer */
	state->bit_buffer |= (bits << state->bits_in_buffer);
	state->bits_in_buffer += num_bits;

	/* Fast path: write all full bytes at once */
	while (state->bits_in_buffer >= 8) {
		if (strm->avail_out == 0) {
			return Z_BUF_ERROR;
		}

		*strm->next_out++ = state->bit_buffer & 0xFF;
		strm->avail_out--;
		strm->total_out++;

		state->bit_buffer >>= 8;
		state->bits_in_buffer -= 8;
	}

	return Z_OK;
}

/* Flush remaining bits in buffer */
static int flush_bits(z_stream *strm, deflate_state *state) {
	/* Flush any remaining bits */
	if (state->bits_in_buffer > 0) {
		if (strm->avail_out == 0) {
			return Z_BUF_ERROR;
		}

		*strm->next_out++ = state->bit_buffer & 0xFF;
		strm->avail_out--;
		strm->total_out++;

		state->bit_buffer = 0;
		state->bits_in_buffer = 0;
	}

	return Z_OK;
}

/* Initialize static Huffman tables for deflate */
static void init_fixed_huffman_deflate(deflate_state *state) {
	/* Same tables as used in inflate */
	for (int i = 0; i < 144; i++) {
		state->literals.codes[i] = 0x30 + i; /* 8 bits, 00110000 + i */
		state->literals.lengths[i] = 8;
	}
	for (int i = 144; i < 256; i++) {
		state->literals.codes[i] = 0x190 + (i - 144); /* 9 bits, 110010000 + (i - 144) */
		state->literals.lengths[i] = 9;
	}
	for (int i = 256; i < 280; i++) {
		state->literals.codes[i] = 0 + (i - 256); /* 7 bits, 0000000 + (i - 256) */
		state->literals.lengths[i] = 7;
	}
	for (int i = 280; i < 288; i++) {
		state->literals.codes[i] = 0xC0 + (i - 280); /* 8 bits, 11000000 + (i - 280) */
		state->literals.lengths[i] = 8;
	}
	state->literals.count = 288;

	/* Distance codes */
	for (int i = 0; i < 32; i++) {
		state->distances.codes[i] = i; /* 5 bits */
		state->distances.lengths[i] = 5;
	}
	state->distances.count = 32;
}

/* Write a Huffman code to output (deflate packs Huffman codes MSB-first) */
static int write_huffman_code(z_stream *strm, deflate_state *state, uint16_t code, uint8_t code_length) {
	uint32_t rev = 0;
	for (int i = 0; i < code_length; i++) {
		rev = (rev << 1) | ((code >> i) & 1);
	}
	return write_bits (strm, state, rev, code_length);
}

/* ----------- Main encoder API functions ----------- */

/* Compatibility wrapper for zlib */
int deflateInit2_(z_stream *strm, int level, int method, int windowBits, int memLevel, int strategy, const char *version, int stream_size) {
	(void)version; /* Unused */
	(void)stream_size; /* Unused */
	return deflateInit2 (strm, level, method, windowBits, memLevel, strategy);
}

int deflateInit2(z_stream *strm, int level, int method, int windowBits, int memLevel, int strategy) {
	if (!strm) {
		return Z_STREAM_ERROR;
	}
	(void)method;
	(void)memLevel;
	(void)strategy;

	/* Validate parameters - handle negative windowBits for raw deflate */
	int abs_windowBits = windowBits < 0? -windowBits: windowBits;
	if (abs_windowBits < 8 || abs_windowBits > 15) {
		return Z_STREAM_ERROR;
	}

	/* Normalize compression level */
	if (level == Z_DEFAULT_COMPRESSION) {
		level = 6;
	}

	/* Allocate state */
	deflate_state *state = (deflate_state *)calloc (1, sizeof (deflate_state));
	if (!state) {
		return Z_MEM_ERROR;
	}

	/* Calculate window size (2^windowBits) */
	state->window_size = 1 << abs_windowBits;
	state->window_mask = state->window_size - 1;

	/* Allocate sliding window */
	state->window = (uint8_t *)malloc (state->window_size);
	if (!state->window) {
		free (state);
		return Z_MEM_ERROR;
	}

	/* Allocate hash table - size depends on window size */
	state->hash_size = 1 << (abs_windowBits - 3); /* Smaller than window for memory efficiency */
	state->hash_mask = state->hash_size - 1;
	state->hash_table = (uint32_t *)calloc (state->hash_size, sizeof (uint32_t));
	if (!state->hash_table) {
		free (state->window);
		free (state);
		return Z_MEM_ERROR;
	}

	/* Initialize state */
	state->level = level;
	state->is_last_block = 0;
	state->pos = 0;
	state->bit_buffer = 0;
	state->bits_in_buffer = 0;

	/* Initialize Huffman tables */
	init_fixed_huffman_deflate (state);

	strm->state = state;
	strm->total_in = 0;
	strm->total_out = 0;

	return Z_OK;
}

int deflate(z_stream *strm, int flush) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}
	deflate_state *state = (deflate_state *)strm->state;

	/* Check if this is the final block */
	if (flush == Z_FINISH) {
		state->is_last_block = 1;
	}

	/* If no compression wanted or very small level, just store uncompressed */
	if (state->level == Z_NO_COMPRESSION) {
		/* Emit one stored block per DEFLATE 64KiB limit until avail_in is drained */
		while (strm->avail_in > 0) {
			uint32_t chunk = strm->avail_in > 0xFFFF? 0xFFFFu: strm->avail_in;
			uint32_t block_size = 4 + chunk;

			/* Check if we have enough output space for this block's len/nlen + data */
			if (strm->avail_out < block_size) {
				return Z_BUF_ERROR;
			}

			/* BFINAL only on the block that drains the last of the input under Z_FINISH */
			int is_final = (flush == Z_FINISH && chunk == strm->avail_in)? 1: 0;

			int ret = write_bits (strm, state, is_final, 1); /* Final block bit */
			if (ret != Z_OK) {
				return ret;
			}

			ret = write_bits (strm, state, 0, 2); /* Block type 00 */
			if (ret != Z_OK) {
				return ret;
			}

			/* Flush remaining bits to achieve byte alignment for uncompressed data */
			flush_bits (strm, state);

			/* Write length and inverted length */
			uint16_t len = (uint16_t)chunk;
			*strm->next_out++ = len & 0xFF;
			*strm->next_out++ = (len >> 8) & 0xFF;
			*strm->next_out++ = (~len) & 0xFF;
			*strm->next_out++ = ((~len) >> 8) & 0xFF;
			strm->avail_out -= 4;
			strm->total_out += 4;

			/* Write data */
			memcpy (strm->next_out, strm->next_in, chunk);
			strm->next_in += chunk;
			strm->avail_in -= chunk;
			strm->total_in += chunk;
			strm->next_out += chunk;
			strm->avail_out -= chunk;
			strm->total_out += chunk;
		}
		return flush == Z_FINISH? Z_STREAM_END: Z_OK;
	}

	if (state->finalized) {
		return flush == Z_FINISH? Z_STREAM_END: Z_OK;
	}

	/* Use fixed Huffman codes for simplicity */
	int block_opened = 0;
	if (strm->avail_in > 0) {
		/* Write block header - type 01 (fixed Huffman) */
		int ret = write_bits (strm, state, state->is_last_block? 1: 0, 1); /* Final block bit */
		if (ret != Z_OK) {
			return ret;
		}

		ret = write_bits (strm, state, 1, 2); /* Block type 01 */
		if (ret != Z_OK) {
			return ret;
		}
		block_opened = 1;

		/* Process all input data */
		while (strm->avail_in > 0) {
			/* Ensure we have output space for a worst-case symbol */
			if (strm->avail_out < 4) { /* Conservative estimate */
				return Z_BUF_ERROR;
			}

			/* Look for a match */
			uint32_t distance = 0;
			uint32_t max_look_ahead = strm->avail_in > 258? 258: strm->avail_in;
			uint32_t match_len = 0;

			/* Skip match search for level < 3 or not enough data */
			if (state->level >= 3 && max_look_ahead >= 3) {
				match_len = find_longest_match (state, strm->next_in, max_look_ahead, &distance);
			}

			if (match_len >= 3) {
				/* Encode length/distance pair */

				/* Determine length code */
				uint16_t length_code;
				uint8_t extra_bits;

				/* Calculate length code and extra bits more efficiently */
				if (match_len <= 10) {
					length_code = 257 + (match_len - 3);
					extra_bits = 0;
				} else if (match_len <= 34) {
					/* Use binary search for mid-range */
					if (match_len <= 18) {
						length_code = 265 + ((match_len - 11) >> 1);
						extra_bits = 1;
					} else {
						length_code = 269 + ((match_len - 19) >> 2);
						extra_bits = 2;
					}
				} else if (match_len <= 130) {
					if (match_len <= 66) {
						length_code = 273 + ((match_len - 35) >> 3);
						extra_bits = 3;
					} else {
						length_code = 277 + ((match_len - 67) >> 4);
						extra_bits = 4;
					}
				} else if (match_len < 258) {
					length_code = 281 + ((match_len - 131) >> 5);
					extra_bits = 5;
				} else {
					length_code = 285; /* 258 has its own code, not 284 + 31 */
					extra_bits = 0;
				}

				/* Write length code */
				write_huffman_code (strm, state, state->literals.codes[length_code], state->literals.lengths[length_code]);

				/* Write extra bits for length if needed */
				if (extra_bits > 0) {
					/* Calculate extra bits value - it's just the remainder when dividing by 2^extra_bits */
					int extra_value = (match_len - (extra_bits == 1? 11: extra_bits == 2? 19
										: extra_bits == 3? 35
										: extra_bits == 4? 67
													: 131)) &
						((1 << extra_bits) - 1);
					write_bits (strm, state, extra_value, extra_bits);
				}

				/* Determine distance code */
				uint16_t distance_code;
				uint8_t dist_extra_bits;

				/* Calculate distance code and extra bits - simplify with binary search */
				if (distance <= 4) {
					distance_code = distance - 1;
					dist_extra_bits = 0;
				} else if (distance <= 256) {
					/* Use binary search for common case */
					if (distance <= 32) {
						if (distance <= 16) {
							if (distance <= 8) {
								distance_code = 4 + ((distance - 5) >> 1);
								dist_extra_bits = 1;
							} else {
								distance_code = 6 + ((distance - 9) >> 2);
								dist_extra_bits = 2;
							}
						} else {
							distance_code = 8 + ((distance - 17) >> 3);
							dist_extra_bits = 3;
						}
					} else if (distance <= 128) {
						if (distance <= 64) {
							distance_code = 10 + ((distance - 33) >> 4);
							dist_extra_bits = 4;
						} else {
							distance_code = 12 + ((distance - 65) >> 5);
							dist_extra_bits = 5;
						}
					} else {
						distance_code = 14 + ((distance - 129) >> 6);
						dist_extra_bits = 6;
					}
				} else {
					/* Calculate for larger distances */
					if (distance <= 4096) {
						if (distance <= 1024) {
							if (distance <= 512) {
								distance_code = 16 + ((distance - 257) >> 7);
								dist_extra_bits = 7;
							} else {
								distance_code = 18 + ((distance - 513) >> 8);
								dist_extra_bits = 8;
							}
						} else if (distance <= 2048) {
							distance_code = 20 + ((distance - 1025) >> 9);
							dist_extra_bits = 9;
						} else {
							distance_code = 22 + ((distance - 2049) >> 10);
							dist_extra_bits = 10;
						}
					} else {
						if (distance <= 16384) {
							if (distance <= 8192) {
								distance_code = 24 + ((distance - 4097) >> 11);
								dist_extra_bits = 11;
							} else {
								distance_code = 26 + ((distance - 8193) >> 12);
								dist_extra_bits = 12;
							}
						} else {
							distance_code = 28 + ((distance - 16385) >> 13);
							dist_extra_bits = 13;
						}
					}
				}

				/* Write distance code */
				write_huffman_code (strm, state, state->distances.codes[distance_code], state->distances.lengths[distance_code]);

				/* Write extra bits for distance if needed */
				if (dist_extra_bits > 0) {
					/* Calculate base distance value based on distance code */
					const uint16_t dist_base_values[] = { 1, 5, 9, 17, 33, 65, 129, 257, 513, 1025, 2049, 4097, 8193, 16385 };
					uint16_t base_dist = dist_base_values[dist_extra_bits];

					/* Get remainder by masking with appropriate bit mask */
					int extra_value = (distance - base_dist) &((1 << dist_extra_bits) - 1);
					write_bits (strm, state, extra_value, dist_extra_bits);
				}

				/* Update input position and window */
				for (uint32_t i = 0; i < match_len; i++) {
					/* Add to sliding window */
					state->window[state->pos & state->window_mask] = *strm->next_in;
					state->pos++;

					/* Update input position */
					strm->next_in++;
					strm->avail_in--;
					strm->total_in++;

					/* If we've used all input, we're done */
					if (strm->avail_in == 0) {
						break;
					}
				}
			} else {
				/* No match, output literal */
				uint8_t literal = *strm->next_in;

				/* Write literal code */
				write_huffman_code (strm, state, state->literals.codes[literal], state->literals.lengths[literal]);

				/* Update input position and window */
				state->window[state->pos & state->window_mask] = literal;
				state->pos++;

				strm->next_in++;
				strm->avail_in--;
				strm->total_in++;
			}
		}

	}

	/* Close the stream with a final block when Z_FINISH was requested.
	 * If no block was opened above (avail_in was 0), open an empty fixed
	 * Huffman block first so the EOB symbol is properly framed. */
	if (flush == Z_FINISH && !state->finalized) {
		if (!block_opened) {
			int ret = write_bits (strm, state, 1, 1); /* Final block bit */
			if (ret != Z_OK) {
				return ret;
			}
			ret = write_bits (strm, state, 1, 2); /* Block type 01 */
			if (ret != Z_OK) {
				return ret;
			}
		}
		/* Write end of block symbol (256) and flush remaining bits. */
		write_huffman_code (strm, state, state->literals.codes[256], state->literals.lengths[256]);
		flush_bits (strm, state);
		state->finalized = 1;
	}

	return flush == Z_FINISH? Z_STREAM_END: Z_OK;
}

int deflateEnd(z_stream *strm) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}

	deflate_state *state = (deflate_state *)strm->state;

	/* Free all allocated memory */
	free (state->window);
	free (state->hash_table);
	free (state);

	strm->state = NULL;
	return Z_OK;
}

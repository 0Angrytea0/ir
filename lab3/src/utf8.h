#pragma once
#include <cstdint>
#include <cstddef>

bool utf8_decode_one(const unsigned char* s, size_t n, size_t* used, uint32_t* cp);
size_t utf8_encode_one(uint32_t cp, unsigned char out[4]);

bool is_token_char(uint32_t cp);
uint32_t to_lower_basic(uint32_t cp);

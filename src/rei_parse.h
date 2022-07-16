#ifndef REI_PARSE_H
#define REI_PARSE_H

#include "rei_types.h"

#define JSMN_STATIC
#include <jsmn/jsmn.h>

typedef struct rei_json_state_t {
  const char* json;
  jsmntok_t* json_tokens;
  const jsmntok_t* current_token;
} rei_json_state_t;

void rei_parse_u32 (const char* src, u32* out);
void rei_parse_u64 (const char* src, u64* out);

rei_result_e rei_json_tokenize (const char* json, u64 json_size, rei_json_state_t* out);

// Skip all the way to the next json token.
void rei_json_skip (rei_json_state_t* state);

void rei_json_parse_u32 (rei_json_state_t* state, u32* out);
void rei_json_parse_u64 (rei_json_state_t* state, u64* out);
void rei_json_parse_floats (rei_json_state_t* state, f32* out);
void rei_json_parse_string (rei_json_state_t* state, rei_string_view_t* out);

b8 rei_json_string_eq (const rei_json_state_t* state, const char* a, const jsmntok_t* b);

#endif /* REI_PARSE_H */

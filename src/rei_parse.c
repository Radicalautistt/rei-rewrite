#include <string.h>

#include "rei_parse.h"
#include "rei_debug.h"
#include "rei_defines.h"

static REI_FORCE_INLINE b8 _s_is_digit (char symbol) {
  return symbol >= '0' && symbol <= '9';
}

void rei_parse_u8 (const char* src, u8* out) {
  *out = 0;

  for (; *src && _s_is_digit (*src); ++src)
    *out = *out * 10 + (u8) (*src - '0');
}

void rei_parse_u32 (const char* src, u32* out) {
  *out = 0;

  for (; *src && _s_is_digit (*src); ++src)
    *out = *out * 10 + (u32) (*src - '0');
}

void rei_parse_u64 (const char* src, u64* out) {
  *out = 0;

  for (; *src && _s_is_digit (*src); ++src)
    *out = *out * 10 + (u64) (*src - '0');
}

rei_result_e rei_json_tokenize (const char* json, u64 json_size, rei_json_state_t* out) {
  jsmn_parser json_parser = {0, 0, 0};

  s32 token_count = jsmn_parse (&json_parser, json, json_size, NULL, 0);
  if (token_count <= 0) return REI_RESULT_INVALID_JSON;

  out->json = json;
  out->json_tokens = malloc (sizeof *out->json_tokens * (u32) token_count);

  jsmn_init (&json_parser);
  jsmn_parse (&json_parser, json, json_size, out->json_tokens, (u32) token_count);

  out->current_token = out->json_tokens;

  return REI_RESULT_SUCCESS;
}

void rei_json_skip (rei_json_state_t* state) {
  ++state->current_token;
  const jsmntok_t* end = state->current_token + 1;

  while (state->current_token < end) {
    switch (state->current_token->type) {
      case JSMN_OBJECT: end += state->current_token->size * 2; break;
      case JSMN_ARRAY: end += state->current_token->size; break;
      default: break;
    }

    ++state->current_token;
  }
}

void rei_json_parse_u32 (rei_json_state_t* state, u32* out) {
  ++state->current_token;
  REI_ASSERT (state->current_token->type == JSMN_PRIMITIVE);

  rei_parse_u32 (state->json + state->current_token->start, out);

  ++state->current_token;
}

void rei_json_parse_u64 (rei_json_state_t* state, u64* out) {
  ++state->current_token;
  REI_ASSERT (state->current_token->type == JSMN_PRIMITIVE);

  rei_parse_u64 (state->json + state->current_token->start, out);

  ++state->current_token;
}

void rei_json_parse_floats (rei_json_state_t* state, f32* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  for (s32 i = 0; i < root_token->size; ++i) {
    out[i] = (f32) atof (state->json + state->current_token->start);
    ++state->current_token;
  }
}

void rei_json_parse_string (rei_json_state_t* state, rei_string_view_t* out) {
  ++state->current_token;
  REI_ASSERT (state->current_token->type == JSMN_STRING);

  const char* start = state->json + state->current_token->start;
  const char* end = start;
  while (*end != '\"') ++end;

  out->size = (u64) (end - start);
  out->src = start;

  ++state->current_token;
}

b8 rei_json_string_eq (const rei_json_state_t* state, const char* a, const jsmntok_t* b) {
  return !strncmp (state->json + b->start, a, strlen (a));
}

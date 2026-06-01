#include "mm_sha256.h"

typedef struct {
  uint32_t state[8];
  uint64_t bit_count;
  uint8_t buffer[64];
  size_t buffer_len;
} mm_sha256_ctx_t;

static const uint32_t k_sha256_round_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t mm_rotr32(uint32_t value, uint32_t shift) {
  return (value >> shift) | (value << (32u - shift));
}

static uint32_t mm_load_be32(const uint8_t *src) {
  return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
         ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

static void mm_store_be32(uint32_t value, uint8_t *dst) {
  dst[0] = (uint8_t)(value >> 24);
  dst[1] = (uint8_t)(value >> 16);
  dst[2] = (uint8_t)(value >> 8);
  dst[3] = (uint8_t)value;
}

static void mm_sha256_transform(mm_sha256_ctx_t *ctx, const uint8_t block[64]) {
  uint32_t schedule[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  int index;

  for (index = 0; index < 16; ++index)
    schedule[index] = mm_load_be32(block + (index * 4));

  for (index = 16; index < 64; ++index) {
    uint32_t s0 = mm_rotr32(schedule[index - 15], 7) ^
                  mm_rotr32(schedule[index - 15], 18) ^
                  (schedule[index - 15] >> 3);
    uint32_t s1 = mm_rotr32(schedule[index - 2], 17) ^
                  mm_rotr32(schedule[index - 2], 19) ^
                  (schedule[index - 2] >> 10);
    schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  for (index = 0; index < 64; ++index) {
    uint32_t s1 = mm_rotr32(e, 6) ^ mm_rotr32(e, 11) ^ mm_rotr32(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 =
        h + s1 + ch + k_sha256_round_constants[index] + schedule[index];
    uint32_t s0 = mm_rotr32(a, 2) ^ mm_rotr32(a, 13) ^ mm_rotr32(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void mm_sha256_init(mm_sha256_ctx_t *ctx) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->state[0] = 0x6a09e667u;
  ctx->state[1] = 0xbb67ae85u;
  ctx->state[2] = 0x3c6ef372u;
  ctx->state[3] = 0xa54ff53au;
  ctx->state[4] = 0x510e527fu;
  ctx->state[5] = 0x9b05688cu;
  ctx->state[6] = 0x1f83d9abu;
  ctx->state[7] = 0x5be0cd19u;
}

static void mm_sha256_update(mm_sha256_ctx_t *ctx, const uint8_t *data,
                             size_t len) {
  size_t remaining = len;
  const uint8_t *cursor = data;

  if (!ctx || (!cursor && remaining != 0))
    return;

  ctx->bit_count += (uint64_t)remaining * 8u;

  while (remaining > 0) {
    size_t chunk = 64u - ctx->buffer_len;
    if (chunk > remaining)
      chunk = remaining;

    memcpy(ctx->buffer + ctx->buffer_len, cursor, chunk);
    ctx->buffer_len += chunk;
    cursor += chunk;
    remaining -= chunk;

    if (ctx->buffer_len == 64u) {
      mm_sha256_transform(ctx, ctx->buffer);
      ctx->buffer_len = 0;
    }
  }
}

static void mm_sha256_final(mm_sha256_ctx_t *ctx, uint8_t digest[32]) {
  uint8_t padding[64];
  uint8_t bit_count_be[8];
  uint64_t bit_count;
  size_t pad_len;
  int index;

  memset(padding, 0, sizeof(padding));
  padding[0] = 0x80u;

  bit_count = ctx->bit_count;
  for (index = 7; index >= 0; --index) {
    bit_count_be[index] = (uint8_t)(bit_count & 0xffu);
    bit_count >>= 8;
  }

  pad_len = (ctx->buffer_len < 56u) ? (56u - ctx->buffer_len)
                                    : (120u - ctx->buffer_len);
  mm_sha256_update(ctx, padding, pad_len);
  mm_sha256_update(ctx, bit_count_be, sizeof(bit_count_be));

  for (index = 0; index < 8; ++index)
    mm_store_be32(ctx->state[index], digest + (index * 4));
}

void mm_sha256_first8_hex_string(const char *text, char out[9]) {
  static const char hex[] = "0123456789abcdef";
  mm_sha256_ctx_t ctx;
  uint8_t digest[32];
  int index;

  if (!out)
    return;

  mm_sha256_init(&ctx);
  if (text)
    mm_sha256_update(&ctx, (const uint8_t *)text, strlen(text));
  mm_sha256_final(&ctx, digest);

  for (index = 0; index < 4; ++index) {
    out[index * 2] = hex[(digest[index] >> 4) & 0x0f];
    out[index * 2 + 1] = hex[digest[index] & 0x0f];
  }
  out[8] = '\0';
}

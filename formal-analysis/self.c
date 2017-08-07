#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monocypher.h"
#include "sha512.h"

#include "chacha20.h"
#include "argon2i.h"
#include "blake2b.h"
#include "blake2b_easy.h"
#include "ed25519_key.h"
#include "ed25519_sign.h"
#include "h_chacha20.h"
#include "key_exchange.h"
#include "poly1305.h"
#include "v_sha512.h"
#include "x25519.h"
#include "x_chacha20.h"

#define FOR(i, start, end) for (size_t (i) = (start); (i) < (end); (i)++)
typedef  int8_t   i8;
typedef uint8_t   u8;
typedef uint32_t u32;
typedef  int32_t i32;
typedef  int64_t i64;
typedef uint64_t u64;

/////////////////
/// Utilities ///
/////////////////

static void* alloc(size_t size)
{
    void *buf = malloc(size);
    if (buf == NULL) {
        fprintf(stderr, "Allocation failed\n");
        exit(1);
    }
    return buf;
}

typedef struct {
    u8     *buf;
    size_t  size;
} vector;

static int test(void (*f)(const vector[], vector*),
                const char *name, size_t nb_inputs,
                size_t nb_vectors, u8 **vectors, size_t *sizes)
{
    int     status   = 0;
    int     nb_tests = 0;
    size_t  idx      = 0;
    vector *in;
    in = (vector*)alloc(nb_vectors * sizeof(vector));
    while (idx < nb_vectors) {
        size_t out_size = sizes[idx + nb_inputs];
        vector out;
        out.buf  = (u8*)alloc(out_size);
        out.size = out_size;
        FOR (i, 0, nb_inputs) {
            in[i].buf  = vectors[idx+i];
            in[i].size = sizes  [idx+i];
        }
        f(in, &out);
        vector expected;
        expected.buf  = vectors[idx+nb_inputs];
        expected.size = sizes  [idx+nb_inputs];
        status |= out.size - expected.size;
        status |= crypto_memcmp(out.buf, expected.buf, out.size);
        free(out.buf);
        idx += nb_inputs + 1;
        nb_tests++;
    }
    free(in);
    printf("%s %3d tests: %s\n",
           status != 0 ? "FAILED" : "OK", nb_tests, name);
    return status;
}

#define TEST(name, nb_inputs) test(name, #name, nb_inputs, \
                                   nb_##name##_vectors,    \
                                   name##_vectors,         \
                                   name##_sizes)

////////////////////////
/// The tests proper ///
////////////////////////
static void chacha20(const vector in[], vector *out)
{
    const vector *key   = in;
    const vector *nonce = in + 1;
    crypto_chacha_ctx ctx;
    crypto_chacha20_init  (&ctx, key->buf, nonce->buf);
    crypto_chacha20_stream(&ctx, out->buf, out->size);
}

static void h_chacha20(const vector in[], vector *out)
{
    const vector *key   = in;
    const vector *input = in + 1;
    crypto_chacha20_H(out->buf, key->buf, input->buf);
}

static void x_chacha20(const vector in[], vector *out)
{
    const vector *key   = in;
    const vector *nonce = in + 1;
    crypto_chacha_ctx ctx;
    crypto_chacha20_x_init(&ctx, key->buf, nonce->buf);
    crypto_chacha20_stream(&ctx, out->buf, out->size);
}

static void blake2b(const vector in[], vector *out)
{
    const vector *msg = in;
    const vector *key = in + 1;
    crypto_blake2b_general(out->buf, out->size,
                           key->buf, key->size,
                           msg->buf, msg->size);
}

static void blake2b_easy(const vector in[], vector *out)
{
    crypto_blake2b(out->buf, in->buf, in->size);
}

static void poly1305(const vector in[], vector *out)
{
    const vector *key = in;
    const vector *msg = in + 1;
    crypto_poly1305_auth(out->buf, msg->buf, msg->size, key->buf);
}

static void argon2i(const vector in[], vector *out)
{
    u32 nb_blocks = 0;
    u32 nb_iterations = 0;
    FOR (i, 0, in[0].size) {nb_blocks     <<= 8; nb_blocks     += in[0].buf[i];}
    FOR (i, 0, in[1].size) {nb_iterations <<= 8; nb_iterations += in[1].buf[i];}
    const vector *password      = in + 2;
    const vector *salt          = in + 3;
    const vector *key           = in + 4;
    const vector *ad            = in + 5;
    void         *work_area     = alloc(nb_blocks * 1024);
    crypto_argon2i(out->buf, out->size,
                   work_area, nb_blocks, nb_iterations,
                   password->buf, password->size,
                   salt    ->buf, salt    ->size,
                   key     ->buf, key     ->size,
                   ad      ->buf, ad      ->size);
    free(work_area);
}

static void x25519(const vector in[], vector *out)
{
    const vector *scalar = in;
    const vector *point  = in + 1;
    int report   = crypto_x25519(out->buf, scalar->buf, point->buf);
    int not_zero = crypto_zerocmp(out->buf, out->size);
    if ( not_zero &&  report)  printf("FAILURE: x25519 false all_zero report\n");
    if (!not_zero && !report)  printf("FAILURE: x25519 failed to report zero\n");
}

static void iterate_x25519(u8 k[32], u8 u[32])
{
    u8 tmp[32];
    crypto_x25519(tmp , k, u);
    memcpy(u, k  , 32);
    memcpy(k, tmp, 32);
}

static int test_x25519()
{
    u8 _1   [32] = {0x42, 0x2c, 0x8e, 0x7a, 0x62, 0x27, 0xd7, 0xbc,
                    0xa1, 0x35, 0x0b, 0x3e, 0x2b, 0xb7, 0x27, 0x9f,
                    0x78, 0x97, 0xb8, 0x7b, 0xb6, 0x85, 0x4b, 0x78,
                    0x3c, 0x60, 0xe8, 0x03, 0x11, 0xae, 0x30, 0x79};
    u8 k[32] = {9};
    u8 u[32] = {9};

    crypto_x25519_public_key(k, u);
    int status = crypto_memcmp(k, _1, 32);
    printf("%s: x25519 1\n", status != 0 ? "FAILED" : "OK");

    u8 _1k  [32] = {0x68, 0x4c, 0xf5, 0x9b, 0xa8, 0x33, 0x09, 0x55,
                    0x28, 0x00, 0xef, 0x56, 0x6f, 0x2f, 0x4d, 0x3c,
                    0x1c, 0x38, 0x87, 0xc4, 0x93, 0x60, 0xe3, 0x87,
                    0x5f, 0x2e, 0xb9, 0x4d, 0x99, 0x53, 0x2c, 0x51};
    FOR (i, 1, 1000) { iterate_x25519(k, u); }
    status |= crypto_memcmp(k, _1k, 32);
    printf("%s: x25519 1K\n", status != 0 ? "FAILED" : "OK");

    // too long; didn't run
    //u8 _100k[32] = {0x7c, 0x39, 0x11, 0xe0, 0xab, 0x25, 0x86, 0xfd,
    //                0x86, 0x44, 0x97, 0x29, 0x7e, 0x57, 0x5e, 0x6f,
    //                0x3b, 0xc6, 0x01, 0xc0, 0x88, 0x3c, 0x30, 0xdf,
    //                0x5f, 0x4d, 0xd2, 0xd2, 0x4f, 0x66, 0x54, 0x24};
    //FOR (i, 1000, 1000000) { iterate_x25519(k, u); }
    //status |= crypto_memcmp(k, _100k, 32);
    //printf("%s: x25519 1M\n", status != 0 ? "FAILED" : "OK");
    return status;
}

static void v_sha512(const vector in[], vector *out)
{
    crypto_sha512(out->buf, in->buf, in->size);
}

static void ed25519_key(const vector in[], vector *out)
{
    crypto_sign_public_key(out->buf, in->buf);
}

static void ed25519_sign(const vector in[], vector *out)
{
    const vector *secret_k = in;
    const vector *public_k = in + 1;
    const vector *msg      = in + 2;
    u8            out2[64];

    // Sign with cached public key, then by reconstructing the key
    crypto_sign(out->buf, secret_k->buf, public_k->buf, msg->buf, msg->size);
    crypto_sign(out2    , secret_k->buf, 0            , msg->buf, msg->size);
    // Compare signatures (must be the same)
    if (crypto_memcmp(out->buf, out2, out->size)) {
        printf("FAILURE: reconstructing public key"
               " yields different signature\n");
    }

    // test successful signature verification
    if (crypto_check(out->buf, public_k->buf, msg->buf, msg->size)) {
        printf("FAILURE: signature check failed to recognise signature\n");
    }
    // test forgery rejections
    u8 fake_signature1[64];
    u8 fake_signature2[64];
    FOR (i, 0, 64) {
        fake_signature1[i] = out->buf[i] + 1;
        fake_signature2[i] = out->buf[i] + 1;
    }
    if (!crypto_check(fake_signature1, public_k->buf, msg->buf, msg->size) ||
        !crypto_check(fake_signature2, public_k->buf, msg->buf, msg->size)) {
        printf("FAILURE: signature check failed to reject forgery\n");
    }
}

static void key_exchange(const vector in[], vector *out)
{
    const vector *secret_key = in;
    const vector *public_key = in + 1;
    crypto_key_exchange(out->buf, secret_key->buf, public_key->buf);
}



//////////////////////////////
/// Self consistency tests ///
//////////////////////////////
// Deterministic "random" number generator, so we can make "random", yet
// reproducible tests.  To change the random stream, change the seed.
static void p_random(u8 *stream, size_t size)
{
    static crypto_chacha_ctx ctx;
    static int is_init = 0;
    if (!is_init) {
        static const u8 seed[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        crypto_chacha20_init(&ctx, seed, seed);
        is_init = 1;
    }
    crypto_chacha20_stream(&ctx, stream, size);
}

// Random 64 bit number
static u64 rand64()
{
    u8  tmp;
    u64 result = 0;
    FOR (i, 0, 8) {
        p_random(&tmp, 1);
        result <<= 8;
        result  += tmp;
    }
    return result;
}



// Tests that encrypting in chunks yields the same result than
// encrypting all at once.
static int p_chacha20()
{
    static const size_t block_size = 64;             // Chacha Block size
    static const size_t input_size = block_size * 4; // total input size
    static const size_t c_max_size = block_size * 2; // maximum chunk size
    int status = 0;
    FOR (i, 0, 1000) {
        size_t offset = 0;
        // outputs
        u8 output_chunk[input_size];
        u8 output_whole[input_size];
        // inputs
        u8 input       [input_size];  p_random(input, input_size);
        u8 key         [32];          p_random(key  , 32);
        u8 nonce       [8];           p_random(nonce, 8);

        // Encrypt in chunks
        crypto_chacha_ctx ctx;
        crypto_chacha20_init(&ctx, key, nonce);
        while (1) {
            size_t chunk_size = rand64() % c_max_size;
            if (offset + chunk_size > input_size) { break; }
            u8 *out = output_chunk + offset;
            u8 *in  = input        + offset;
            crypto_chacha20_encrypt(&ctx, out, in, chunk_size);
            offset += chunk_size;
        }
        // Encrypt all at once
        crypto_chacha20_init(&ctx, key, nonce);
        crypto_chacha20_encrypt(&ctx, output_whole, input, offset);

        // Compare the results (must be the same)
        status |= crypto_memcmp(output_chunk, output_whole, offset);
    }
    printf("%s: Chacha20\n", status != 0 ? "FAILED" : "OK");
    return status;
}

static int p_chacha20_set_ctr()
{
    static const size_t nb_blocks   = 10;
    static const size_t block_size  = 64;
    static const size_t stream_size = block_size * nb_blocks;
    int status = 0;
    FOR (i, 0, 1000) {
        u8 output_part[stream_size    ];
        u8 output_all [stream_size    ];
        u8 output_more[stream_size * 2];
        u8 key        [32];          p_random(key  , 32);
        u8 nonce      [8];           p_random(nonce, 8 );
        size_t ctr   = rand64() % nb_blocks;
        size_t limit = ctr * block_size;
        // Encrypt all at once
        crypto_chacha_ctx ctx;
        crypto_chacha20_init(&ctx, key, nonce);
        crypto_chacha20_stream(&ctx, output_all, stream_size);
        // Encrypt second part
        crypto_chacha20_set_ctr(&ctx, ctr);
        crypto_chacha20_stream(&ctx, output_part + limit, stream_size - limit);
        // Encrypt first part
        crypto_chacha20_set_ctr(&ctx, 0);
        crypto_chacha20_stream(&ctx, output_part, limit);
        // Compare the results (must be the same)
        status |= crypto_memcmp(output_part, output_all, stream_size);

        // Encrypt before the begining
        crypto_chacha20_set_ctr(&ctx, -ctr);
        crypto_chacha20_stream(&ctx,
                               output_more + stream_size - limit,
                               stream_size + limit);
        // Compare the results (must be the same)
        status |= crypto_memcmp(output_more + stream_size,
                                output_all,
                                stream_size);
    }
    printf("%s: Chacha20 (set counter)\n", status != 0 ? "FAILED" : "OK");
    return status;
}

// Tests that authenticating bit by bit yields the same mac than
// authenticating all at once
static int p_poly1305()
{
    static const size_t block_size = 16;             // poly1305 block size
    static const size_t input_size = block_size * 4; // total input size
    static const size_t c_max_size = block_size * 2; // maximum chunk size
    int status = 0;
    FOR (i, 0, 1000) {
        size_t offset = 0;
        // outputs
        u8 mac_chunk[16];
        u8 mac_whole[16];
        // inputs
        u8 input[input_size];  p_random(input, input_size);
        u8 key  [32];          p_random(key  , 32);

        // Authenticate bit by bit
        crypto_poly1305_ctx ctx;
        crypto_poly1305_init(&ctx, key);
        while (1) {
            size_t chunk_size = rand64() % c_max_size;
            if (offset + chunk_size > input_size) { break; }
            crypto_poly1305_update(&ctx, input + offset, chunk_size);
            offset += chunk_size;
        }
        crypto_poly1305_final(&ctx, mac_chunk);

        // Authenticate all at once
        crypto_poly1305_auth(mac_whole, input, offset, key);

        // Compare the results (must be the same)
        status |= crypto_memcmp(mac_chunk, mac_whole, 16);
    }
    printf("%s: Poly1305\n", status != 0 ? "FAILED" : "OK");
    return status;
}

// Tests that hashing bit by bit yields the same hash than hashing all
// at once.  Note: I figured we didn't need to test keyed mode, or
// different hash sizes, again.  This test sticks to the simplified
// interface.
static int p_blake2b()
{
    static const size_t block_size = 128;            // Blake2b block size
    static const size_t input_size = block_size * 4; // total input size
    static const size_t c_max_size = block_size * 2; // maximum chunk size
    int status = 0;
    FOR (i, 0, 1000) {
        size_t offset = 0;
        // outputs
        u8 hash_chunk[64];
        u8 hash_whole[64];
        // inputs
        u8 input[input_size];  p_random(input, input_size);

        // Authenticate bit by bit
        crypto_blake2b_ctx ctx;
        crypto_blake2b_init(&ctx);
        while (1) {
            size_t chunk_size = rand64() % c_max_size;
            if (offset + chunk_size > input_size) { break; }
            crypto_blake2b_update(&ctx, input + offset, chunk_size);
            offset += chunk_size;
        }
        crypto_blake2b_final(&ctx, hash_chunk);

        // Authenticate all at once
        crypto_blake2b(hash_whole, input, offset);

        // Compare the results (must be the same)
        status |= crypto_memcmp(hash_chunk, hash_whole, 64);
    }
    printf("%s: Blake2b\n", status != 0 ? "FAILED" : "OK");
    return status;
}

// Tests that hashing bit by bit yields the same hash than hashing all
// at once. (for sha512)
static int p_sha512()
{
    static const size_t block_size = 128;            // Blake2b block size
    static const size_t input_size = block_size * 4; // total input size
    static const size_t c_max_size = block_size * 2; // maximum chunk size
    int status = 0;
    FOR (i, 0, 1000) {
        size_t offset = 0;
        // outputs
        u8 hash_chunk[64];
        u8 hash_whole[64];
        // inputs
        u8 input[input_size];  p_random(input, input_size);

        // Authenticate bit by bit
        crypto_sha512_ctx ctx;
        crypto_sha512_init(&ctx);
        while (1) {
            size_t chunk_size = rand64() % c_max_size;
            if (offset + chunk_size > input_size) { break; }
            crypto_sha512_update(&ctx, input + offset, chunk_size);
            offset += chunk_size;
        }
        crypto_sha512_final(&ctx, hash_chunk);

        // Authenticate all at once
        crypto_sha512(hash_whole, input, offset);

        // Compare the results (must be the same)
        status |= crypto_memcmp(hash_chunk, hash_whole, 64);
    }
    printf("%s: Sha512\n", status != 0 ? "FAILED" : "OK");
    return status;
}

static int p_aead()
{
    int status = 0;
    FOR (i, 0, 1000) {
        u8 key      [32];  p_random(key      , 32);
        u8 nonce    [24];  p_random(nonce    , 24);
        u8 ad       [ 4];  p_random(ad       ,  4);
        u8 plaintext[ 8];  p_random(plaintext,  8);
        u8 box[24], box2[24];
        u8 out[8];
        // AEAD roundtrip
        crypto_aead_lock(box, box+16, key, nonce, ad, 4, plaintext, 8);
        status |= crypto_aead_unlock(out, key, nonce, box, ad, 4, box+16, 8);
        status |= crypto_memcmp(plaintext, out, 8);
        box[0]++;
        status |= !crypto_aead_unlock(out, key, nonce, box, ad, 4, box+16, 8);

        // Authenticated roundtrip (easy interface)
        // Make and accept message
        crypto_lock(box, box + 16, key, nonce, plaintext, 8);
        status |= crypto_unlock(out, key, nonce, box, box + 16, 8);
        // Make sure decrypted text and original text are the same
        status |= crypto_memcmp(plaintext, out, 8);
        // Make and reject forgery
        box[0]++;
        status |= !crypto_unlock(out, key, nonce, box, box + 16, 8);
        box[0]--; // undo forgery

        // Same result for both interfaces
        crypto_aead_lock(box2, box2 + 16, key, nonce, 0, 0, plaintext, 8);
        status |= crypto_memcmp(box, box2, 24);
    }
    printf("%s: aead\n", status != 0 ? "FAILED" : "OK");
    return status;
}

int main(void)
{
    int status = 0;
    printf("\nTest against vectors");
    printf("\n--------------------\n");
    status |= TEST(chacha20    , 2);
    status |= TEST(h_chacha20  , 2);
    status |= TEST(x_chacha20  , 2);
    status |= TEST(blake2b     , 2);
    status |= TEST(blake2b_easy, 1);
    status |= TEST(poly1305    , 2);
    status |= TEST(argon2i     , 6);
    status |= TEST(x25519      , 2);
    status |= TEST(key_exchange, 2);
    status |= TEST(v_sha512    , 1);
    status |= TEST(ed25519_key , 1);
    status |= TEST(ed25519_sign, 3);
    status |= test_x25519();

    printf("\nProperty based tests tests");
    printf("\n--------------------------\n");
    status |= p_chacha20();
    status |= p_chacha20_set_ctr();
    status |= p_poly1305();
    status |= p_blake2b();
    status |= p_sha512();
    status |= p_aead();

    return status;
}

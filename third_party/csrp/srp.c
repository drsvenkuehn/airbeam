/*
 * SRP-6a implementation using OpenSSL bignum API.
 * See srp.h for licence text.
 *
 * Group parameters: RFC 5054 §A.1 3072-bit group (N, g=5), SHA-512.
 * This matches the HAP (HomeKit Accessory Protocol) pairing requirements.
 */

#include "srp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Suppress OpenSSL deprecation warnings on Windows */
#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif

#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

/* ========================================================================= */
/* Groups                                                                     */
/* ========================================================================= */

/* RFC 5054 Appendix A — 3072-bit group */
static const char* k_SRP_3072_N_hex =
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
    "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
    "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
    "670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
    "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9"
    "DE2BCBF6955817183995497CEA956AE515D2261898FA0510"
    "15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64"
    "ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7"
    "ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B"
    "F12FFA06D98A0864D87602733EC86A64521F2B18177B200C"
    "BBE117577A615D6C770988C0BAD946E208E24FA074E5AB31"
    "43DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF";

static const char* k_SRP_3072_g_hex = "05";

/* ========================================================================= */
/* Internal helpers                                                           */
/* ========================================================================= */

typedef struct {
    BIGNUM* N;
    BIGNUM* g;
} NGConstant;

static NGConstant* new_ng(SRP_NGType ng_type,
                          const char* n_hex, const char* g_hex)
{
    NGConstant* ng = (NGConstant*)malloc(sizeof(NGConstant));
    if (!ng) return NULL;
    ng->N = BN_new();
    ng->g = BN_new();
    if (!ng->N || !ng->g) {
        BN_free(ng->N); BN_free(ng->g); free(ng);
        return NULL;
    }

    const char* nstr = (ng_type == SRP_NG_CUSTOM && n_hex) ? n_hex : k_SRP_3072_N_hex;
    const char* gstr = (ng_type == SRP_NG_CUSTOM && g_hex) ? g_hex : k_SRP_3072_g_hex;

    if (!BN_hex2bn(&ng->N, nstr) || !BN_hex2bn(&ng->g, gstr)) {
        BN_free(ng->N); BN_free(ng->g); free(ng);
        return NULL;
    }
    return ng;
}

static void delete_ng(NGConstant* ng)
{
    if (!ng) return;
    BN_free(ng->N);
    BN_free(ng->g);
    free(ng);
}

/* Hash (SHA-512) of concatenated BIGNUMs, padding each to len(N) bytes.
 * output must be SHA512_DIGEST_LENGTH bytes.
 */
static void H_bn_bn(const BIGNUM* N, const BIGNUM* n1, const BIGNUM* n2,
                    unsigned char* output)
{
    const int nbytes = BN_num_bytes(N);
    unsigned char* buf = (unsigned char*)malloc((size_t)nbytes * 2);
    if (!buf) return;
    memset(buf, 0, (size_t)nbytes * 2);
    BN_bn2binpad(n1, buf,          nbytes);
    BN_bn2binpad(n2, buf + nbytes, nbytes);
    SHA512(buf, (size_t)nbytes * 2, output);
    free(buf);
}

/* H(bytes) */
static void H_bytes(const unsigned char* bytes, size_t len, unsigned char* out)
{
    SHA512(bytes, len, out);
}

/* H(N) xor H(g) */
static void H_N_xor_g(const BIGNUM* N, const BIGNUM* g, unsigned char* out)
{
    unsigned char hN[SHA512_DIGEST_LENGTH];
    unsigned char hg[SHA512_DIGEST_LENGTH];
    int nbytes = BN_num_bytes(N);
    unsigned char* bN = (unsigned char*)malloc((size_t)nbytes);
    unsigned char  bg[1] = { 0 };
    if (!bN) return;
    BN_bn2bin(N, bN);
    H_bytes(bN, (size_t)nbytes, hN);
    /* g as 1-byte */
    bg[0] = (unsigned char)BN_bn2bin(g, bg);
    H_bytes(bg, 1, hg);
    free(bN);
    for (int i = 0; i < SHA512_DIGEST_LENGTH; i++)
        out[i] = hN[i] ^ hg[i];
}

/* Compute k = H(N, g) */
static BIGNUM* compute_k(const BIGNUM* N, const BIGNUM* g)
{
    unsigned char digest[SHA512_DIGEST_LENGTH];
    H_bn_bn(N, N, g, digest);
    return BN_bin2bn(digest, SHA512_DIGEST_LENGTH, NULL);
}

/* Compute x = H(s, H(username:password)) */
static BIGNUM* compute_x(const unsigned char* s, int ls,
                          const char* username,
                          const unsigned char* password, int lp)
{
    unsigned char inner[SHA512_DIGEST_LENGTH];
    unsigned char outer[SHA512_DIGEST_LENGTH];
    SHA512_CTX ctx;
    /* inner = H(username ":" password) */
    SHA512_Init(&ctx);
    SHA512_Update(&ctx, username, strlen(username));
    SHA512_Update(&ctx, ":", 1);
    SHA512_Update(&ctx, password, (size_t)lp);
    SHA512_Final(inner, &ctx);
    /* outer = H(s || inner) */
    SHA512_Init(&ctx);
    SHA512_Update(&ctx, s, (size_t)ls);
    SHA512_Update(&ctx, inner, SHA512_DIGEST_LENGTH);
    SHA512_Final(outer, &ctx);
    return BN_bin2bn(outer, SHA512_DIGEST_LENGTH, NULL);
}

/* Compute M1 = H(H(N) XOR H(g), H(I), s, A, B, K)  */
static void compute_M1(const BIGNUM* N, const BIGNUM* g,
                       const char* username,
                       const unsigned char* s,  int ls,
                       const unsigned char* A,  int lA,
                       const unsigned char* B,  int lB,
                       const unsigned char* K,  int lK,
                       unsigned char* out)   /* SHA512_DIGEST_LENGTH */
{
    unsigned char HNxorHg[SHA512_DIGEST_LENGTH];
    unsigned char HI[SHA512_DIGEST_LENGTH];
    H_N_xor_g(N, g, HNxorHg);
    H_bytes((const unsigned char*)username, strlen(username), HI);
    SHA512_CTX ctx;
    SHA512_Init(&ctx);
    SHA512_Update(&ctx, HNxorHg, SHA512_DIGEST_LENGTH);
    SHA512_Update(&ctx, HI,      SHA512_DIGEST_LENGTH);
    SHA512_Update(&ctx, s,  (size_t)ls);
    SHA512_Update(&ctx, A,  (size_t)lA);
    SHA512_Update(&ctx, B,  (size_t)lB);
    SHA512_Update(&ctx, K,  (size_t)lK);
    SHA512_Final(out, &ctx);
}

/* Compute HAMK = H(A, M1, K) */
static void compute_HAMK(const unsigned char* A,   int lA,
                         const unsigned char* M1,
                         const unsigned char* K,   int lK,
                         unsigned char* out)  /* SHA512_DIGEST_LENGTH */
{
    SHA512_CTX ctx;
    SHA512_Init(&ctx);
    SHA512_Update(&ctx, A,  (size_t)lA);
    SHA512_Update(&ctx, M1, SHA512_DIGEST_LENGTH);
    SHA512_Update(&ctx, K,  (size_t)lK);
    SHA512_Final(out, &ctx);
}

/* u = H(A, B) */
static BIGNUM* compute_u(const BIGNUM* N,
                         const unsigned char* A, int lA,
                         const unsigned char* B, int lB)
{
    unsigned char dig[SHA512_DIGEST_LENGTH];
    SHA512_CTX ctx;
    SHA512_Init(&ctx);
    SHA512_Update(&ctx, A, (size_t)lA);
    SHA512_Update(&ctx, B, (size_t)lB);
    SHA512_Final(dig, &ctx);
    return BN_bin2bn(dig, SHA512_DIGEST_LENGTH, NULL);
}

/* ========================================================================= */
/* Public API — shared key helpers                                            */
/* ========================================================================= */

void srp_free(void* p) { free(p); }

void srp_create_salted_verification_key(
    SRP_HashAlgorithm   alg,
    SRP_NGType          ng_type,
    const char*         username,
    const unsigned char* password,
    int                 len_password,
    unsigned char**     bytes_s,
    int*                len_s,
    unsigned char**     bytes_v,
    int*                len_v,
    const char*         n_hex,
    const char*         g_hex)
{
    (void)alg; /* only SHA-512 supported */
    *bytes_s = NULL; *len_s = 0;
    *bytes_v = NULL; *len_v = 0;

    NGConstant* ng = new_ng(ng_type, n_hex, g_hex);
    if (!ng) return;

    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) { delete_ng(ng); return; }

    /* Generate random 16-byte salt */
    const int salt_len = 16;
    unsigned char* salt = (unsigned char*)malloc((size_t)salt_len);
    if (!salt || RAND_bytes(salt, salt_len) != 1) {
        free(salt); BN_CTX_free(ctx); delete_ng(ng); return;
    }

    BIGNUM* x = compute_x(salt, salt_len, username, password, len_password);
    BIGNUM* v = BN_new();
    if (!x || !v) {
        free(salt); BN_free(x); BN_free(v); BN_CTX_free(ctx); delete_ng(ng); return;
    }

    /* v = g^x mod N */
    BN_mod_exp(v, ng->g, x, ng->N, ctx);

    const int vlen = BN_num_bytes(v);
    unsigned char* vbytes = (unsigned char*)malloc((size_t)vlen);
    if (!vbytes) {
        free(salt); BN_free(x); BN_free(v); BN_CTX_free(ctx); delete_ng(ng); return;
    }
    BN_bn2bin(v, vbytes);

    *bytes_s = salt;   *len_s = salt_len;
    *bytes_v = vbytes; *len_v = vlen;

    BN_free(x); BN_free(v);
    BN_CTX_free(ctx);
    delete_ng(ng);
}

/* ========================================================================= */
/* Verifier (server-side)                                                     */
/* ========================================================================= */

struct SRPVerifier {
    char*           username;
    unsigned char   session_key[SHA512_DIGEST_LENGTH];
    unsigned char   M[SHA512_DIGEST_LENGTH];
    unsigned char   H_AMK[SHA512_DIGEST_LENGTH];
    int             authenticated;
    /* raw bytes for B */
    unsigned char*  bytes_B;
    int             len_B;
    /* raw bytes for A (kept for HAMK) */
    unsigned char*  bytes_A;
    int             len_A;
    NGConstant*     ng;
};

SRPVerifier* srp_verifier_new(
    SRP_HashAlgorithm   alg,
    SRP_NGType          ng_type,
    const char*         username,
    const unsigned char* bytes_s, int len_s,
    const unsigned char* bytes_v, int len_v,
    const unsigned char* bytes_A, int len_A,
    const unsigned char** bytes_B, int* len_B,
    const char*         n_hex,
    const char*         g_hex)
{
    (void)alg;
    *bytes_B = NULL; *len_B = 0;

    if (!bytes_A || len_A == 0) return NULL;

    NGConstant* ng = new_ng(ng_type, n_hex, g_hex);
    if (!ng) return NULL;

    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) { delete_ng(ng); return NULL; }

    BIGNUM* A = BN_bin2bn(bytes_A, len_A, NULL);
    BIGNUM* v = BN_bin2bn(bytes_v, len_v, NULL);
    BIGNUM* k = compute_k(ng->N, ng->g);
    BIGNUM* b = BN_new();     /* server private ephemeral */
    BIGNUM* B_bn = BN_new();  /* server public ephemeral */
    BIGNUM* kv  = BN_new();
    BIGNUM* gB  = BN_new();
    BIGNUM* tmp = BN_new();

    if (!A || !v || !k || !b || !B_bn || !kv || !gB || !tmp) goto fail;

    /* A must not be zero mod N */
    BN_mod(tmp, A, ng->N, ctx);
    if (BN_is_zero(tmp)) goto fail;

    /* b: random 256-bit private ephemeral */
    if (!BN_rand(b, 256, -1, 0)) goto fail;

    /* B = (k*v + g^b) mod N */
    BN_mod_exp(gB, ng->g, b, ng->N, ctx);
    BN_mod_mul(kv, k, v, ng->N, ctx);
    BN_mod_add(B_bn, kv, gB, ng->N, ctx);

    /* Encode A and B to raw bytes */
    const int nlen = BN_num_bytes(ng->N);
    unsigned char* aBytes = (unsigned char*)calloc(1, (size_t)nlen);
    unsigned char* bBytes = (unsigned char*)calloc(1, (size_t)nlen);
    if (!aBytes || !bBytes) { free(aBytes); free(bBytes); goto fail; }
    BN_bn2binpad(A,    aBytes, nlen);
    BN_bn2binpad(B_bn, bBytes, nlen);

    /* u = H(A, B) */
    BIGNUM* u = compute_u(ng->N, aBytes, nlen, bBytes, nlen);
    if (!u || BN_is_zero(u)) {
        free(aBytes); free(bBytes); BN_free(u); goto fail;
    }

    /* S = (A * v^u) ^ b mod N */
    BIGNUM* vu = BN_new();
    BIGNUM* Avu = BN_new();
    BIGNUM* S  = BN_new();
    if (!vu || !Avu || !S) {
        free(aBytes); free(bBytes); BN_free(u);
        BN_free(vu); BN_free(Avu); BN_free(S); goto fail;
    }
    BN_mod_exp(vu, v, u, ng->N, ctx);
    BN_mod_mul(Avu, A, vu, ng->N, ctx);
    BN_mod_exp(S, Avu, b, ng->N, ctx);

    /* K = H(S) */
    {
        int slen = BN_num_bytes(S);
        unsigned char* sbytes = (unsigned char*)calloc(1, (size_t)nlen);
        BN_bn2binpad(S, sbytes, nlen);
        SHA512(sbytes, (size_t)nlen, NULL); /* side-effect silence */
        SHA512(sbytes, (size_t)nlen, NULL);
        /* Actually: K = SHA512(S padded to nlen) */
        unsigned char Kbuf[SHA512_DIGEST_LENGTH];
        SHA512(sbytes, (size_t)nlen, Kbuf);
        free(sbytes);
        (void)slen;

        SRPVerifier* ver = (SRPVerifier*)calloc(1, sizeof(SRPVerifier));
        if (!ver) {
            free(aBytes); free(bBytes);
            BN_free(u); BN_free(vu); BN_free(Avu); BN_free(S);
            goto fail;
        }
        ver->username = (char*)malloc(strlen(username) + 1);
        if (!ver->username) { free(ver); free(aBytes); free(bBytes);
            BN_free(u); BN_free(vu); BN_free(Avu); BN_free(S); goto fail; }
        strcpy(ver->username, username);
        memcpy(ver->session_key, Kbuf, SHA512_DIGEST_LENGTH);
        ver->authenticated = 0;
        ver->bytes_B = bBytes;
        ver->len_B   = nlen;
        ver->bytes_A = aBytes;
        ver->len_A   = nlen;
        ver->ng = ng;

        /* Pre-compute expected M1 */
        compute_M1(ng->N, ng->g, username, bytes_s, len_s,
                   aBytes, nlen, bBytes, nlen,
                   Kbuf, SHA512_DIGEST_LENGTH, ver->M);

        *bytes_B = ver->bytes_B;
        *len_B   = ver->len_B;

        BN_free(A); BN_free(v); BN_free(k); BN_free(b);
        BN_free(B_bn); BN_free(kv); BN_free(gB); BN_free(tmp);
        BN_free(u); BN_free(vu); BN_free(Avu); BN_free(S);
        BN_CTX_free(ctx);
        return ver;
    }

fail:
    BN_free(A); BN_free(v); BN_free(k); BN_free(b);
    BN_free(B_bn); BN_free(kv); BN_free(gB); BN_free(tmp);
    BN_CTX_free(ctx);
    delete_ng(ng);
    return NULL;
}

void srp_verifier_delete(SRPVerifier* ver)
{
    if (!ver) return;
    delete_ng(ver->ng);
    free(ver->username);
    free(ver->bytes_B);
    free(ver->bytes_A);
    free(ver);
}

int srp_verifier_is_authenticated(SRPVerifier* ver)
{
    return ver ? ver->authenticated : 0;
}

const char* srp_verifier_get_username(SRPVerifier* ver)
{
    return ver ? ver->username : NULL;
}

const unsigned char* srp_verifier_get_session_key(SRPVerifier* ver, int* key_length)
{
    if (!ver || !ver->authenticated) { if (key_length) *key_length = 0; return NULL; }
    if (key_length) *key_length = SHA512_DIGEST_LENGTH;
    return ver->session_key;
}

void srp_verifier_verify_session(
    SRPVerifier*          ver,
    const unsigned char*  user_M,
    const unsigned char** bytes_HAMK)
{
    *bytes_HAMK = NULL;
    if (!ver || !user_M) return;
    if (memcmp(user_M, ver->M, SHA512_DIGEST_LENGTH) != 0) return;
    ver->authenticated = 1;
    compute_HAMK(ver->bytes_A, ver->len_A, ver->M,
                 ver->session_key, SHA512_DIGEST_LENGTH, ver->H_AMK);
    *bytes_HAMK = ver->H_AMK;
}

/* ========================================================================= */
/* User (client-side)                                                         */
/* ========================================================================= */

struct SRPUser {
    char*           username;
    unsigned char*  password;
    int             len_password;
    unsigned char   session_key[SHA512_DIGEST_LENGTH];
    unsigned char   M[SHA512_DIGEST_LENGTH];
    unsigned char   H_AMK[SHA512_DIGEST_LENGTH];
    int             authenticated;
    /* Ephemeral */
    BIGNUM*         a;      /* private */
    BIGNUM*         A;      /* public  */
    /* Raw bytes of A */
    unsigned char*  bytes_A;
    int             len_A;
    NGConstant*     ng;
};

SRPUser* srp_user_new(
    SRP_HashAlgorithm   alg,
    SRP_NGType          ng_type,
    const char*         username,
    const unsigned char* bytes_password, int len_password,
    const char*         n_hex,
    const char*         g_hex)
{
    (void)alg;
    NGConstant* ng = new_ng(ng_type, n_hex, g_hex);
    if (!ng) return NULL;

    SRPUser* usr = (SRPUser*)calloc(1, sizeof(SRPUser));
    if (!usr) { delete_ng(ng); return NULL; }

    usr->username = (char*)malloc(strlen(username) + 1);
    usr->password = (unsigned char*)malloc((size_t)len_password);
    if (!usr->username || !usr->password) {
        free(usr->username); free(usr->password); free(usr); delete_ng(ng); return NULL;
    }
    strcpy(usr->username, username);
    memcpy(usr->password, bytes_password, (size_t)len_password);
    usr->len_password  = len_password;
    usr->authenticated = 0;
    usr->ng = ng;
    return usr;
}

void srp_user_delete(SRPUser* usr)
{
    if (!usr) return;
    BN_free(usr->a); BN_free(usr->A);
    free(usr->bytes_A);
    /* Zero password before freeing */
    if (usr->password) {
        memset(usr->password, 0, (size_t)usr->len_password);
        free(usr->password);
    }
    free(usr->username);
    delete_ng(usr->ng);
    free(usr);
}

int srp_user_is_authenticated(SRPUser* usr) { return usr ? usr->authenticated : 0; }
const char* srp_user_get_username(SRPUser* usr) { return usr ? usr->username : NULL; }

const unsigned char* srp_user_get_session_key(SRPUser* usr, int* key_length)
{
    if (!usr || !usr->authenticated) { if (key_length) *key_length = 0; return NULL; }
    if (key_length) *key_length = SHA512_DIGEST_LENGTH;
    return usr->session_key;
}

int srp_user_get_session_key_length(SRPUser* usr)
{
    return (usr && usr->authenticated) ? SHA512_DIGEST_LENGTH : 0;
}

void srp_user_start_authentication(
    SRPUser*        usr,
    const char**    auth_username,
    unsigned char** bytes_A,
    int*            len_A)
{
    *auth_username = NULL; *bytes_A = NULL; *len_A = 0;
    if (!usr) return;

    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) return;

    /* a: 256-bit private ephemeral */
    usr->a = BN_new();
    usr->A = BN_new();
    if (!usr->a || !usr->A) { BN_CTX_free(ctx); return; }

    if (!BN_rand(usr->a, 256, -1, 0)) { BN_CTX_free(ctx); return; }

    /* A = g^a mod N */
    BN_mod_exp(usr->A, usr->ng->g, usr->a, usr->ng->N, ctx);
    BN_CTX_free(ctx);

    const int nlen = BN_num_bytes(usr->ng->N);
    usr->bytes_A = (unsigned char*)calloc(1, (size_t)nlen);
    if (!usr->bytes_A) return;
    BN_bn2binpad(usr->A, usr->bytes_A, nlen);
    usr->len_A = nlen;

    *auth_username = usr->username;
    *bytes_A       = usr->bytes_A;
    *len_A         = usr->len_A;
}

void srp_user_process_challenge(
    SRPUser*             usr,
    const unsigned char* bytes_s,  int len_s,
    const unsigned char* bytes_B,  int len_B,
    unsigned char**      bytes_M,
    int*                 len_M)
{
    *bytes_M = NULL; *len_M = 0;
    if (!usr || !bytes_B || len_B == 0) return;

    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) return;

    const int nlen = BN_num_bytes(usr->ng->N);

    BIGNUM* B = BN_bin2bn(bytes_B, len_B, NULL);
    BIGNUM* k = compute_k(usr->ng->N, usr->ng->g);
    BIGNUM* x = compute_x(bytes_s, len_s, usr->username,
                           usr->password, usr->len_password);

    if (!B || !k || !x) goto fail;

    /* Pad B to nlen for u computation */
    unsigned char* bPad = (unsigned char*)calloc(1, (size_t)nlen);
    if (!bPad) goto fail;
    BN_bn2binpad(B, bPad, nlen);

    BIGNUM* u = compute_u(usr->ng->N, usr->bytes_A, usr->len_A, bPad, nlen);
    if (!u || BN_is_zero(u)) { free(bPad); BN_free(u); goto fail; }

    /* B must not be 0 mod N */
    {
        BIGNUM* tmp = BN_new();
        BN_mod(tmp, B, usr->ng->N, ctx);
        const int isZero = BN_is_zero(tmp);
        BN_free(tmp);
        if (isZero) { free(bPad); BN_free(u); goto fail; }
    }

    /* S = (B - k * g^x) ^ (a + u*x) mod N */
    BIGNUM* gx  = BN_new();
    BIGNUM* kgx = BN_new();
    BIGNUM* ux  = BN_new();
    BIGNUM* aux = BN_new();
    BIGNUM* Bkgx = BN_new();
    BIGNUM* S    = BN_new();
    if (!gx || !kgx || !ux || !aux || !Bkgx || !S) {
        free(bPad); BN_free(u);
        BN_free(gx); BN_free(kgx); BN_free(ux); BN_free(aux); BN_free(Bkgx); BN_free(S);
        goto fail;
    }
    BN_mod_exp(gx,  usr->ng->g, x, usr->ng->N, ctx);
    BN_mod_mul(kgx, k, gx, usr->ng->N, ctx);
    BN_mod_sub(Bkgx, B, kgx, usr->ng->N, ctx);
    BN_mul(ux,  u, x, ctx);
    BN_add(aux, usr->a, ux);
    BN_mod_exp(S, Bkgx, aux, usr->ng->N, ctx);

    /* K = H(S) */
    {
        unsigned char* sPad = (unsigned char*)calloc(1, (size_t)nlen);
        BN_bn2binpad(S, sPad, nlen);
        SHA512(sPad, (size_t)nlen, usr->session_key);
        free(sPad);
    }

    /* M1 = H(H(N)^H(g), H(I), s, A, B, K) */
    compute_M1(usr->ng->N, usr->ng->g, usr->username,
               bytes_s, len_s,
               usr->bytes_A, usr->len_A,
               bPad, nlen,
               usr->session_key, SHA512_DIGEST_LENGTH,
               usr->M);

    free(bPad);
    BN_free(u); BN_free(gx); BN_free(kgx); BN_free(ux);
    BN_free(aux); BN_free(Bkgx); BN_free(S);
    BN_free(B); BN_free(k); BN_free(x);
    BN_CTX_free(ctx);

    *bytes_M = usr->M;
    *len_M   = SHA512_DIGEST_LENGTH;
    return;

fail:
    BN_free(B); BN_free(k); BN_free(x);
    BN_CTX_free(ctx);
}

void srp_user_verify_session(SRPUser* usr, const unsigned char* bytes_HAMK)
{
    if (!usr || !bytes_HAMK) return;
    /* Compute expected HAMK */
    unsigned char expected[SHA512_DIGEST_LENGTH];
    compute_HAMK(usr->bytes_A, usr->len_A, usr->M,
                 usr->session_key, SHA512_DIGEST_LENGTH, expected);
    if (memcmp(bytes_HAMK, expected, SHA512_DIGEST_LENGTH) == 0)
        usr->authenticated = 1;
}

const unsigned char* srp_user_get_shared_key(SRPUser* usr, int* len)
{
    if (!usr || !usr->authenticated) { if (len) *len = 0; return NULL; }
    if (len) *len = SHA512_DIGEST_LENGTH;
    return usr->session_key;
}

/*
 * Secure Remote Password 6a implementation
 * Copyright (c) 2010, Tom Cocagne. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This is an implementation of SRP-6a as described in:
 *   http://srp.stanford.edu/design.html
 *
 * Notes: This library does not perform any user name / password validation.
 *        Refer to http://srp.stanford.edu/demo/demo.html for test vectors.
 */

#ifndef CCSRP_H
#define CCSRP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Size of the H(A|M|K) hash output — SHA-512 = 64 bytes */
#define SRP_SHA512_DIGEST_LENGTH 64

typedef enum
{
    SRP_SHA1   = 0,
    SRP_SHA224 = 1,
    SRP_SHA256 = 2,
    SRP_SHA384 = 3,
    SRP_SHA512 = 4
} SRP_HashAlgorithm;

typedef enum
{
    SRP_NG_1024 = 0,
    SRP_NG_2048 = 1,
    SRP_NG_3072 = 5,  /* RFC 5054 3072-bit (HAP default) */
    SRP_NG_4096 = 2,
    SRP_NG_8192 = 3,
    SRP_NG_CUSTOM = 4
} SRP_NGType;

typedef struct SRPVerifier  SRPVerifier;
typedef struct SRPUser      SRPUser;

/* Output parameters are newly allocated. It is the caller's responsibility
 * to free them. Output parameters set to NULL / 0 indicate failure.
 *
 * salt:     out — random salt.  Caller must free with srp_free().
 * bytes_v:  out — verifier.  Caller must free with srp_free().
 */
void srp_create_salted_verification_key(
    SRP_HashAlgorithm alg,
    SRP_NGType        ng_type,
    const char*       username,
    const unsigned char* password,
    int               len_password,
    unsigned char**   bytes_s,   /* OUT: salt (must free) */
    int*              len_s,     /* OUT: salt length */
    unsigned char**   bytes_v,   /* OUT: verifier (must free) */
    int*              len_v,     /* OUT: verifier length */
    const char*       n_hex,     /* NULL for built-in group */
    const char*       g_hex      /* NULL for built-in group */
);

/* Free memory allocated by this library. */
void srp_free(void* p);

/* --------------------------------------------------------------------- */
/* Verifier (server-side)                                                */
/* --------------------------------------------------------------------- */

/* Returns NULL on failure.
 * bytes_B and len_B are the public server ephemeral.  They remain
 * valid until srp_verifier_delete() is called.
 */
SRPVerifier* srp_verifier_new(
    SRP_HashAlgorithm  alg,
    SRP_NGType         ng_type,
    const char*        username,
    const unsigned char* bytes_s, int len_s,
    const unsigned char* bytes_v, int len_v,
    const unsigned char* bytes_A, int len_A,
    const unsigned char** bytes_B, int* len_B,
    const char*        n_hex,
    const char*        g_hex
);

void srp_verifier_delete(SRPVerifier* ver);

int  srp_verifier_is_authenticated(SRPVerifier* ver);

const char* srp_verifier_get_username(SRPVerifier* ver);

/* Returns session key (HAMK) after successful authentication, else NULL */
const unsigned char* srp_verifier_get_session_key(SRPVerifier* ver, int* key_length);

/* Verify client's M1 proof; returns HAMK (server's M2 proof) or NULL on failure.
 * bytes_HAMK remains valid until srp_verifier_delete() is called.
 */
void srp_verifier_verify_session(
    SRPVerifier*         ver,
    const unsigned char* user_M,
    const unsigned char** bytes_HAMK
);

/* --------------------------------------------------------------------- */
/* User (client-side)                                                    */
/* --------------------------------------------------------------------- */

SRPUser* srp_user_new(
    SRP_HashAlgorithm  alg,
    SRP_NGType         ng_type,
    const char*        username,
    const unsigned char* bytes_password, int len_password,
    const char*        n_hex,
    const char*        g_hex
);

void srp_user_delete(SRPUser* usr);

int  srp_user_is_authenticated(SRPUser* usr);

const char* srp_user_get_username(SRPUser* usr);

/* Returns session key after successful authentication, else NULL */
const unsigned char* srp_user_get_session_key(SRPUser* usr, int* key_length);

int srp_user_get_session_key_length(SRPUser* usr);

/* Start authentication; fills bytes_A / len_A with the client public ephemeral.
 * Caller must free bytes_A with srp_free().
 */
void srp_user_start_authentication(
    SRPUser*       usr,
    const char**   auth_username,  /* OUT: username to send */
    unsigned char** bytes_A,       /* OUT: client public key A (must free) */
    int*            len_A
);

/* Process server's challenge (s/B).
 * Returns M1 (client proof) in bytes_M / len_M — valid until next call or delete.
 */
void srp_user_process_challenge(
    SRPUser*             usr,
    const unsigned char* bytes_s,  int len_s,
    const unsigned char* bytes_B,  int len_B,
    unsigned char**      bytes_M,  /* OUT: M1 proof (must free) */
    int*                 len_M
);

/* Verify server's M2 (HAMK) proof. */
void srp_user_verify_session(
    SRPUser*             usr,
    const unsigned char* bytes_HAMK
);

/* Shared session key (available after successful verify) */
const unsigned char* srp_user_get_shared_key(SRPUser* usr, int* len);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* CCSRP_H */

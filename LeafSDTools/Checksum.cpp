#include "stdafx.h"
#include "Checksum.h"

// ---------------------------------------------------------------------------
// Self-contained MD5 / SHA-512 implementation.
//
// Windows CE on the QY7XXX/QY8XXX head units does not ship a usable OpenSSL or
// CNG provider, so the hashing primitives are implemented here from scratch.
// Both algorithms are processed incrementally so we never need to load the
// whole (up to ~128 MB) NAND image into RAM at once.
// ---------------------------------------------------------------------------

typedef unsigned char      u8;
typedef unsigned int       u32;
typedef unsigned __int64   u64;

// ----------------------------- MD5 -----------------------------------------

typedef struct {
    u32 state[4];
    u64 count;        // number of bytes processed
    u8  buffer[64];
} MD5_CTX;

#define MD5_F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))
#define MD5_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void MD5_Init(MD5_CTX* c) {
    c->count = 0;
    c->state[0] = 0x67452301;
    c->state[1] = 0xefcdab89;
    c->state[2] = 0x98badcfe;
    c->state[3] = 0x10325476;
}

static void MD5_Transform(u32 state[4], const u8 block[64]) {
    static const u32 K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    static const u32 S[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    };

    u32 M[16];
    int i;
    for (i = 0; i < 16; i++) {
        M[i] = (u32)block[i*4] | ((u32)block[i*4+1] << 8) |
               ((u32)block[i*4+2] << 16) | ((u32)block[i*4+3] << 24);
    }

    u32 a = state[0], b = state[1], c = state[2], d = state[3];
    for (i = 0; i < 64; i++) {
        u32 f;
        int g;
        if (i < 16)      { f = MD5_F(b, c, d); g = i; }
        else if (i < 32) { f = MD5_G(b, c, d); g = (5*i + 1) & 15; }
        else if (i < 48) { f = MD5_H(b, c, d); g = (3*i + 5) & 15; }
        else             { f = MD5_I(b, c, d); g = (7*i) & 15; }

        u32 tmp = d;
        d = c;
        c = b;
        b = b + MD5_ROTL(a + f + K[i] + M[g], S[i]);
        a = tmp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

static void MD5_Update(MD5_CTX* c, const u8* data, u32 len) {
    u32 index = (u32)(c->count & 63);
    c->count += len;
    u32 partLen = 64 - index;
    u32 i = 0;
    if (len >= partLen) {
        memcpy(&c->buffer[index], data, partLen);
        MD5_Transform(c->state, c->buffer);
        for (i = partLen; i + 63 < len; i += 64)
            MD5_Transform(c->state, &data[i]);
        index = 0;
    }
    memcpy(&c->buffer[index], &data[i], len - i);
}

static void MD5_Final(MD5_CTX* c, u8 digest[16]) {
    u8 padding[64];
    memset(padding, 0, sizeof(padding));
    padding[0] = 0x80;

    u64 bitCount = c->count * 8;
    u8 lenBytes[8];
    int i;
    for (i = 0; i < 8; i++)
        lenBytes[i] = (u8)((bitCount >> (8*i)) & 0xFF);

    u32 index = (u32)(c->count & 63);
    u32 padLen = (index < 56) ? (56 - index) : (120 - index);
    MD5_Update(c, padding, padLen);
    MD5_Update(c, lenBytes, 8);

    for (i = 0; i < 4; i++) {
        digest[i*4]   = (u8)(c->state[i] & 0xFF);
        digest[i*4+1] = (u8)((c->state[i] >> 8) & 0xFF);
        digest[i*4+2] = (u8)((c->state[i] >> 16) & 0xFF);
        digest[i*4+3] = (u8)((c->state[i] >> 24) & 0xFF);
    }
}

// ---------------------------- SHA-512 --------------------------------------

typedef struct {
    u64 state[8];
    u64 count;        // number of bytes processed (low 64 bits are plenty here)
    u8  buffer[128];
} SHA512_CTX;

#define SHA512_ROTR(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define SHA512_S0(x) (SHA512_ROTR(x,28) ^ SHA512_ROTR(x,34) ^ SHA512_ROTR(x,39))
#define SHA512_S1(x) (SHA512_ROTR(x,14) ^ SHA512_ROTR(x,18) ^ SHA512_ROTR(x,41))
#define SHA512_s0(x) (SHA512_ROTR(x,1)  ^ SHA512_ROTR(x,8)  ^ ((x) >> 7))
#define SHA512_s1(x) (SHA512_ROTR(x,19) ^ SHA512_ROTR(x,61) ^ ((x) >> 6))
#define SHA512_CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define SHA512_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

static const u64 SHA512_K[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
    0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

static void SHA512_Init(SHA512_CTX* c) {
    c->count = 0;
    c->state[0] = 0x6a09e667f3bcc908ULL;
    c->state[1] = 0xbb67ae8584caa73bULL;
    c->state[2] = 0x3c6ef372fe94f82bULL;
    c->state[3] = 0xa54ff53a5f1d36f1ULL;
    c->state[4] = 0x510e527fade682d1ULL;
    c->state[5] = 0x9b05688c2b3e6c1fULL;
    c->state[6] = 0x1f83d9abfb41bd6bULL;
    c->state[7] = 0x5be0cd19137e2179ULL;
}

static void SHA512_Transform(u64 state[8], const u8 block[128]) {
    u64 w[80];
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((u64)block[i*8]   << 56) | ((u64)block[i*8+1] << 48) |
               ((u64)block[i*8+2] << 40) | ((u64)block[i*8+3] << 32) |
               ((u64)block[i*8+4] << 24) | ((u64)block[i*8+5] << 16) |
               ((u64)block[i*8+6] << 8)  | ((u64)block[i*8+7]);
    }
    for (i = 16; i < 80; i++)
        w[i] = SHA512_s1(w[i-2]) + w[i-7] + SHA512_s0(w[i-15]) + w[i-16];

    u64 a = state[0], b = state[1], cc = state[2], d = state[3];
    u64 e = state[4], f = state[5], g = state[6], h = state[7];

    for (i = 0; i < 80; i++) {
        u64 t1 = h + SHA512_S1(e) + SHA512_CH(e, f, g) + SHA512_K[i] + w[i];
        u64 t2 = SHA512_S0(a) + SHA512_MAJ(a, b, cc);
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += cc; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g;  state[7] += h;
}

static void SHA512_Update(SHA512_CTX* c, const u8* data, u32 len) {
    u32 index = (u32)(c->count & 127);
    c->count += len;
    u32 partLen = 128 - index;
    u32 i = 0;
    if (len >= partLen) {
        memcpy(&c->buffer[index], data, partLen);
        SHA512_Transform(c->state, c->buffer);
        for (i = partLen; i + 127 < len; i += 128)
            SHA512_Transform(c->state, &data[i]);
        index = 0;
    }
    memcpy(&c->buffer[index], &data[i], len - i);
}

static void SHA512_Final(SHA512_CTX* c, u8 digest[64]) {
    u8 padding[128];
    memset(padding, 0, sizeof(padding));
    padding[0] = 0x80;

    u64 bitCount = c->count * 8;
    u8 lenBytes[16];
    memset(lenBytes, 0, sizeof(lenBytes));
    int i;
    // 128-bit big-endian length; only the low 64 bits are populated which is
    // far beyond any image size we deal with here.
    for (i = 0; i < 8; i++)
        lenBytes[15 - i] = (u8)((bitCount >> (8*i)) & 0xFF);

    u32 index = (u32)(c->count & 127);
    u32 padLen = (index < 112) ? (112 - index) : (240 - index);
    SHA512_Update(c, padding, padLen);
    SHA512_Update(c, lenBytes, 16);

    for (i = 0; i < 8; i++) {
        int j;
        for (j = 0; j < 8; j++)
            digest[i*8 + j] = (u8)((c->state[i] >> (56 - 8*j)) & 0xFF);
    }
}

// ----------------------------- Helpers -------------------------------------

static void BytesToHex(const u8* bytes, int len, char* out) {
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 0; i < len; i++) {
        out[i*2]   = hex[(bytes[i] >> 4) & 0xF];
        out[i*2+1] = hex[bytes[i] & 0xF];
    }
    out[len*2] = '\0';
}

bool ComputeFileHash(int algo, const char* filePath, char* hexOut, int hexOutSize) {
    if (filePath == NULL || hexOut == NULL) return false;

    int digestLen = (algo == HASH_SHA512) ? 64 : 16;
    if (hexOutSize < digestLen * 2 + 1) return false;

    FILE* f = fopen(filePath, "rb");
    if (f == NULL) return false;

    const u32 CHUNK = 64 * 1024;
    u8* buffer = (u8*)malloc(CHUNK);
    if (buffer == NULL) {
        fclose(f);
        return false;
    }

    MD5_CTX md5;
    SHA512_CTX sha;
    if (algo == HASH_SHA512) SHA512_Init(&sha);
    else                     MD5_Init(&md5);

    size_t read;
    while ((read = fread(buffer, 1, CHUNK, f)) > 0) {
        if (algo == HASH_SHA512) SHA512_Update(&sha, buffer, (u32)read);
        else                     MD5_Update(&md5, buffer, (u32)read);
    }

    free(buffer);
    fclose(f);

    u8 digest[64];
    if (algo == HASH_SHA512) SHA512_Final(&sha, digest);
    else                     MD5_Final(&md5, digest);

    BytesToHex(digest, digestLen, hexOut);
    return true;
}

void GetSidecarFileName(int algo, const char* filePath, char* out, int outSize) {
    const char* ext = (algo == HASH_SHA512) ? ".sha512" : ".md5";
    _snprintf(out, outSize, "%s%s", filePath, ext);
    out[outSize - 1] = '\0';
}

bool WriteChecksumSidecar(int algo, const char* filePath) {
    char hex[129];
    if (!ComputeFileHash(algo, filePath, hex, sizeof(hex)))
        return false;

    char sidecar[300];
    GetSidecarFileName(algo, filePath, sidecar, sizeof(sidecar));

    FILE* f = fopen(sidecar, "wb");
    if (f == NULL) return false;

    // Write "<hex>  <basename>\n" similar to the md5sum/sha512sum tools.
    const char* base = filePath;
    const char* p = filePath;
    while (*p) {
        if (*p == '\\' || *p == '/') base = p + 1;
        p++;
    }
    fprintf(f, "%s  %s\n", hex, base);
    fclose(f);
    return true;
}

bool VerifyChecksumSidecar(int algo, const char* filePath, bool* sidecarMissing) {
    if (sidecarMissing) *sidecarMissing = false;

    char sidecar[300];
    GetSidecarFileName(algo, filePath, sidecar, sizeof(sidecar));

    FILE* f = fopen(sidecar, "rb");
    if (f == NULL) {
        if (sidecarMissing) *sidecarMissing = true;
        return false;
    }

    char expected[160];
    memset(expected, 0, sizeof(expected));
    // The digest is the first whitespace-delimited token.
    if (fscanf(f, "%159s", expected) != 1) {
        fclose(f);
        return false;
    }
    fclose(f);

    char actual[129];
    if (!ComputeFileHash(algo, filePath, actual, sizeof(actual)))
        return false;

    // Case-insensitive compare of the hex digests.
    int i;
    for (i = 0; expected[i] != '\0' && actual[i] != '\0'; i++) {
        if (tolower((unsigned char)expected[i]) != tolower((unsigned char)actual[i]))
            return false;
    }
    return expected[i] == '\0' && actual[i] == '\0';
}

// ----------------------------- NK.bin (B000FF) -----------------------------

static u32 ReadU32LE(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void SetErr(char* errOut, int errOutSize, const char* msg) {
    if (errOut && errOutSize > 0) {
        strncpy(errOut, msg, errOutSize - 1);
        errOut[errOutSize - 1] = '\0';
    }
}

bool ValidateNkBin(const char* filePath, char* errOut, int errOutSize) {
    // B000FF layout:
    //   7-byte magic "B000FF\n"
    //   header: start_addr (u32 LE), length (u32 LE)
    //   records: { address (u32), length (u32), checksum (u32) } + length data bytes
    //   terminator record: address == 0, length holds the exec/jump address
    static const u8 MAGIC[7] = {'B','0','0','0','F','F','\n'};

    FILE* f = fopen(filePath, "rb");
    if (f == NULL) {
        SetErr(errOut, errOutSize, "cannot open file");
        return false;
    }

    u8 hdr[7 + 8];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        SetErr(errOut, errOutSize, "file too small");
        fclose(f);
        return false;
    }

    if (memcmp(hdr, MAGIC, 7) != 0) {
        SetErr(errOut, errOutSize, "bad magic (not a B000FF NK.bin)");
        fclose(f);
        return false;
    }

    u32 imgStart = ReadU32LE(hdr + 7);
    u32 imgLen   = ReadU32LE(hdr + 11);
    (void)imgStart;
    (void)imgLen;

    const u32 CHUNK = 64 * 1024;
    u8* buffer = (u8*)malloc(CHUNK);
    if (buffer == NULL) {
        SetErr(errOut, errOutSize, "out of memory");
        fclose(f);
        return false;
    }

    bool ok = false;
    int recordCount = 0;
    while (true) {
        u8 rec[12];
        size_t got = fread(rec, 1, 12, f);
        if (got != 12) {
            SetErr(errOut, errOutSize, "truncated record table");
            break;
        }

        u32 addr  = ReadU32LE(rec);
        u32 len   = ReadU32LE(rec + 4);
        u32 csum  = ReadU32LE(rec + 8);

        if (addr == 0) {
            // Terminator record: 'len' is the execution start address, no data.
            ok = (recordCount > 0);
            if (!ok) SetErr(errOut, errOutSize, "no data records before terminator");
            else     SetErr(errOut, errOutSize, "OK");
            break;
        }

        // Verify the byte-sum checksum of this record's payload incrementally.
        u32 sum = 0;
        u32 remaining = len;
        bool readErr = false;
        while (remaining > 0) {
            u32 want = (remaining < CHUNK) ? remaining : CHUNK;
            size_t r = fread(buffer, 1, want, f);
            if (r != want) { readErr = true; break; }
            for (u32 k = 0; k < want; k++)
                sum += buffer[k];
            remaining -= want;
        }

        if (readErr) {
            SetErr(errOut, errOutSize, "truncated record data");
            break;
        }
        if (sum != csum) {
            SetErr(errOut, errOutSize, "record checksum mismatch");
            break;
        }
        recordCount++;
    }

    free(buffer);
    fclose(f);
    return ok;
}

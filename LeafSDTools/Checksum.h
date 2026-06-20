#ifndef CHECKSUM_H
#define CHECKSUM_H

// Hash algorithm selector
enum HashAlgo {
    HASH_MD5 = 0,
    HASH_SHA512 = 1
};

// Compute the hash of a file.
// algo:        HASH_MD5 or HASH_SHA512
// filePath:    full path to the file to hash
// hexOut:      caller-provided buffer that receives the lowercase hex digest
//              (NUL terminated). Must be at least 33 bytes for MD5 and
//              129 bytes for SHA512.
// hexOutSize:  size of hexOut in bytes
// Returns true on success, false on failure (e.g. file could not be opened).
bool ComputeFileHash(int algo, const char* filePath, char* hexOut, int hexOutSize);

// Returns the sidecar file name for a given backup file and algorithm
// (e.g. "\\SystemSD\\nand.bin" -> "\\SystemSD\\nand.bin.sha512").
void GetSidecarFileName(int algo, const char* filePath, char* out, int outSize);

// Compute the hash of filePath and write the hex digest to the matching
// sidecar file. Returns true on success.
bool WriteChecksumSidecar(int algo, const char* filePath);

// Verify filePath against an existing sidecar file.
// Returns true only when the sidecar exists and the digests match.
// If the sidecar is missing, sidecarMissing (when non-NULL) is set to true.
bool VerifyChecksumSidecar(int algo, const char* filePath, bool* sidecarMissing);

// Validate a Windows CE NK.bin (B000FF) boot image.
// Walks the record table, checks the "B000FF\n" magic and verifies that every
// record's stored byte-sum checksum matches the data, as well as that the
// declared image length is consistent.
// errOut (when non-NULL) receives a human readable message describing the
// first problem found (or "OK").
// Returns true only when the image is a structurally valid NK.bin.
bool ValidateNkBin(const char* filePath, char* errOut, int errOutSize);

#endif // CHECKSUM_H

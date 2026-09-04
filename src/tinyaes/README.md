# tinyaes

[![CI Build Tests](https://github.com/gibme-c/tinyaes/actions/workflows/ci.yml/badge.svg)](https://github.com/gibme-c/tinyaes/actions/workflows/ci.yml)

A zero-dependency C++17 [AES](https://en.wikipedia.org/wiki/Advanced_Encryption_Standard) encryption library supporting AES-128, AES-192, and AES-256 in ECB, CBC, CTR, and GCM modes, with SIMD-accelerated backends and runtime CPU dispatch.

AES (Advanced Encryption Standard) is the most widely deployed symmetric cipher, used in TLS, disk encryption, VPNs, and virtually every modern security protocol. This library provides the complete set of block cipher modes you need for real applications -- from raw ECB blocks to authenticated encryption with GCM -- with both a C API and a modern C++ API. On x86_64, AES-NI and VAES hardware instructions are selected at runtime; on ARM64, the Crypto Extensions backend is used. A portable T-table implementation is always available as a fallback.

## Features

### Encryption Modes

- **ECB** -- Electronic Codebook. Block-aligned encrypt/decrypt. Suitable only for single-block operations or as a building block for other modes.
- **CBC** -- Cipher Block Chaining. With or without PKCS#7 padding. Convenience overloads that auto-generate and prepend the IV.
- **CTR** -- Counter mode. Arbitrary-length encrypt/decrypt (no padding needed). Convenience overloads that auto-generate and prepend the nonce.
- **GCM** -- Galois/Counter Mode. Authenticated encryption with associated data (AEAD). Convenience overloads that auto-generate and prepend the nonce and append the authentication tag.

### Key Sizes

- **AES-128** (16-byte key), **AES-192** (24-byte key), **AES-256** (32-byte key)

### SIMD Backends

- **AES-NI + SSE4.1** (x86_64) -- hardware AES round instructions for encrypt/decrypt blocks
- **VAES + AVX-512** (x86_64) -- vectorized AES for CTR pipeline acceleration
- **PCLMULQDQ** (x86_64) -- carry-less multiplication for GHASH (GCM)
- **VPCLMULQDQ + AVX-512** (x86_64) -- vectorized carry-less multiplication for GHASH
- **ARM Crypto Extensions** (aarch64) -- hardware AES and PMULL instructions
- **Portable T-table** -- always compiled, works everywhere

All SIMD backends are selected at runtime via CPUID (x86_64) or platform detection (ARM64). No runtime initialization call is needed -- dispatch happens lazily on first use via atomic function pointers.

### Security

- **Secure key erasure** -- all expanded round key material is zeroed via `secure_zero()` after every API call
- **Constant-time tag verification** -- GCM authentication uses constant-time comparison to prevent timing side-channels
- **Verify-then-decrypt** -- GCM verifies the authentication tag before decrypting; if verification fails, no plaintext is produced
- **Constant-time PKCS#7 unpadding** -- scans the entire last block to prevent padding oracle attacks
- **IV/nonce generation** -- uses `BCryptGenRandom` (Windows), `/dev/urandom` (Linux/macOS), or `arc4random_buf` (BSD)
- **Cross-platform** -- MSVC, GCC, Clang, MinGW

## Building

Requires CMake 3.10+ and a C++17 compiler. No external dependencies.

```bash
# Configure and build
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --config Release -j

# Run tests
./build/tinyaes_tests            # Linux / macOS
./build/Release/tinyaes_tests    # Windows (MSVC)
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `TINYAES_BUILD_TESTS` | `OFF` | Build the unit tests (`tinyaes_tests`) |
| `TINYAES_BUILD_BENCHMARKS` | `OFF` | Build the benchmark tool (`tinyaes_benchmarks`) |
| `TINYAES_BUILD_FUZZERS` | `OFF` | Build libFuzzer fuzzers (Clang + Linux only) |
| `BUILD_SHARED_LIBS` | `OFF` | Build as a shared library instead of static |
| `TINYAES_FORCE_PORTABLE` | `OFF` | Disable all SIMD backends; use only the portable T-table implementation |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug`, `Release`, or `RelWithDebInfo` |

## Usage

Include the master header to access all modes:

```cpp
#include "tinyaes.h"
```

Or include individual headers for specific modes:

```cpp
#include "tinyaes/gcm.h"
#include "tinyaes/ctr.h"
#include "tinyaes/cbc.h"
#include "tinyaes/ecb.h"
```

Link against the `tinyaes` library target in your CMake project:

```cmake
add_subdirectory(tinyaes)
target_link_libraries(your_target tinyaes)
```

### Quick Examples

**GCM authenticated encryption** (recommended for most use cases):

```cpp
#include "tinyaes/gcm.h"

// Encrypt — library generates nonce, output is nonce||ciphertext||tag
std::vector<uint8_t> key(32, 0x42);  // AES-256
std::vector<uint8_t> plaintext = { /* ... */ };
std::vector<uint8_t> aad = { /* associated data */ };
std::vector<uint8_t> output;

auto result = tinyaes::gcm_encrypt(key, plaintext, aad, output);
// output = 12-byte nonce || ciphertext || 16-byte tag

// Decrypt — nonce is first 12 bytes, tag is last 16 bytes
std::vector<uint8_t> decrypted;
result = tinyaes::gcm_decrypt(key, output, aad, decrypted);
if (result != tinyaes::Result::Ok) { /* authentication failed */ }
```

**CTR mode encryption**:

```cpp
#include "tinyaes/ctr.h"

std::vector<uint8_t> key(32, 0x42);
std::vector<uint8_t> plaintext = { /* ... */ };
std::vector<uint8_t> output;

// Encrypt — library generates nonce, prepended to output
auto result = tinyaes::ctr_encrypt(key, plaintext, output);
// output = 12-byte nonce || ciphertext

// Decrypt — nonce is first 12 bytes of input
std::vector<uint8_t> decrypted;
result = tinyaes::ctr_decrypt(key, output, decrypted);
```

**CBC with PKCS#7 padding**:

```cpp
#include "tinyaes/cbc.h"

std::vector<uint8_t> key(16, 0x42);  // AES-128
std::vector<uint8_t> plaintext = { /* ... */ };
std::vector<uint8_t> output;

// Encrypt — library generates IV, prepended to output
auto result = tinyaes::cbc_encrypt_pkcs7(key, plaintext, output);
// output = 16-byte IV || padded ciphertext

// Decrypt — IV is first 16 bytes of input
std::vector<uint8_t> decrypted;
result = tinyaes::cbc_decrypt_pkcs7(key, output, decrypted);
```

### C API

All modes are also available as C functions returning `int` (0 on success):

```c
#include "tinyaes/gcm.h"

uint8_t key[32] = { /* ... */ };
uint8_t nonce[12] = { /* ... */ };
uint8_t tag[16];
uint8_t plaintext[64] = { /* ... */ };
uint8_t ciphertext[64];

int rc = tinyaes_gcm_encrypt(key, 32, nonce, 12, NULL, 0,
                              plaintext, 64, ciphertext, 64, tag);
```

Error codes are defined in `tinyaes/common.h`:

| Code | Constant | Meaning |
|------|----------|---------|
| 0 | `TINYAES_OK` | Success |
| -1 | `TINYAES_INVALID_KEY_SIZE` | Key must be 16, 24, or 32 bytes |
| -2 | `TINYAES_INVALID_IV_SIZE` | IV must be 16 bytes |
| -3 | `TINYAES_INVALID_NONCE_SIZE` | Nonce must be 12 bytes |
| -4 | `TINYAES_INVALID_INPUT_SIZE` | Input not block-aligned (ECB/CBC without padding) |
| -5 | `TINYAES_INVALID_PADDING` | PKCS#7 padding is invalid |
| -6 | `TINYAES_AUTH_FAILED` | GCM authentication tag mismatch |
| -7 | `TINYAES_INTERNAL_ERROR` | Internal error (e.g. IV generation failure) |

## Architecture

### AES Core

The AES implementation uses big-endian round key and state representation throughout, matching the FIPS 197 specification. Key expansion produces the full round key schedule once per API call and securely zeroes it before returning.

Three AES backends are available:

| Backend | Platform | Instructions | Description |
|---------|----------|-------------|-------------|
| **Portable** | All | None | T-table implementation using Te0-Te3 (encrypt) and Td0-Td3 (decrypt) lookup tables |
| **AES-NI** | x86_64 | `aesenc`, `aesenclast`, `aesdec`, `aesdeclast` | Hardware AES round instructions via SSE intrinsics |
| **VAES** | x86_64 | `vaesenc`, `vaesenclast` + AVX-512 | Vectorized AES for multi-block CTR pipeline |
| **ARM CE** | aarch64 | `AESE`, `AESMC`, `AESD`, `AESIMC` | ARM Crypto Extensions for hardware AES |

### GHASH (GCM)

GCM's universal hash function uses GF(2^128) multiplication with MSB-first bit ordering and the reduction polynomial 0xE1000...0.

| Backend | Platform | Instructions | Description |
|---------|----------|-------------|-------------|
| **Portable** | All | None | Bit-by-bit constant-time multiplication |
| **PCLMULQDQ** | x86_64 | `pclmulqdq` | Carry-less multiplication with Karatsuba reduction |
| **VPCLMULQDQ** | x86_64 | `vpclmulqdq` + AVX-512 | Vectorized carry-less multiplication |
| **ARM PMULL** | aarch64 | `PMULL`, `PMULL2` | Polynomial multiplication via NEON |

### Runtime Dispatch

Each function pointer is stored in a `std::atomic` with acquire/release semantics. On first call, CPUID selects the best available backend and stores the function pointer. No mutexes, no `std::call_once`. Redundant resolution under contention is safe by design -- all backends produce identical results.

SIMD compile flags (`-maes`, `-mpclmul`, `-mavx512f`, etc.) are applied only to individual backend source files via `set_source_files_properties` in CMake, never globally. The portable backend always compiles on all platforms.

## Testing

Build with `-DBUILD_TESTS=ON` to get the `tinyaes_tests` executable. The test suite uses a custom test harness (`test_harness.h`) with `ASSERT_EQ`/`TEST` macros -- no external test framework required.

Test coverage includes:

- **Key schedule** -- AES-128/192/256 key expansion against FIPS 197 test vectors
- **ECB** -- NIST test vectors for all three key sizes, encrypt and decrypt
- **CBC** -- encrypt/decrypt with known vectors, PKCS#7 padding/unpadding, block alignment validation
- **CTR** -- NIST test vectors, arbitrary-length encryption, nonce-based convenience API
- **GCM** -- NIST test vectors (multiple key sizes, AAD lengths, plaintext lengths), authentication tag verification, authentication failure rejection
- **PKCS#7 padding** -- correct padding for all block alignments (1-16 bytes), constant-time unpadding validation
- **IV/nonce generation** -- uniqueness and length verification via OS CSPRNG
- **CPUID** -- backend detection and dispatch verification

### Fuzz Testing

Fuzz targets for all four modes (ECB, CBC, CTR, GCM) are included in the `fuzz/` directory. They use [libFuzzer](https://llvm.org/docs/LibFuzzer.html) with AddressSanitizer and are built automatically on Linux with Clang.

## Benchmarking

Build with `-DBUILD_BENCHMARKS=ON` to get the `tinyaes_benchmarks` executable. Measures throughput (MB/s) and cycles/byte for all modes across all key sizes, with both dispatched (best available) and portable backends.

## License

This project is licensed under the [BSD 3-Clause License](LICENSE).

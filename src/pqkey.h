// Copyright (c) 2026 The NEX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef NEX_PQKEY_H
#define NEX_PQKEY_H

#include <hash.h>
#include <serialize.h>
#include <support/allocators/secure.h>
#include <uint256.h>

#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

/**
 * Post-quantum cryptography key classes for NEX.
 *
 * Uses ML-DSA-65 (FIPS 204, formerly CRYSTALS-Dilithium3):
 *   Public key:  1,952 bytes
 *   Secret key:  4,032 bytes
 *   Signature:   3,309 bytes (max; may be slightly smaller)
 *   Security:    NIST Level 3 (~AES-192 equivalent)
 *
 * These classes mirror Bitcoin Core's CKey/CPubKey interface where sensible,
 * but are adapted for PQ key sizes and semantics.
 */

// ── Constants ────────────────────────────────────────────────────────────────

/** ML-DSA-65 sizes from FIPS 204 / PQClean */
static constexpr size_t PQ_PUBKEY_SIZE    = 1952;
static constexpr size_t PQ_SECRETKEY_SIZE = 4032;
static constexpr size_t PQ_SIGNATURE_SIZE = 3309;

/** Hash of a PQ public key: SHA-256(pubkey) = 32 bytes.
 *  Used for address derivation and as a compact identifier. */
static constexpr size_t PQ_PUBKEY_HASH_SIZE = 32;

// ── CPQKeyID ─────────────────────────────────────────────────────────────────

/** A reference to a CPQKey: the SHA-256 hash of the serialized public key.
 *
 *  We use SHA-256 (not Hash160) because:
 *  1. RIPEMD-160 provides only 160-bit collision resistance
 *  2. PQ security targets NIST Level 3 (~192 bits) — 160-bit hash is the bottleneck
 *  3. 32-byte hash gives a cleaner witness program for Bech32m encoding
 */
class CPQKeyID : public uint256
{
public:
    CPQKeyID() : uint256() {}
    explicit CPQKeyID(const uint256& in) : uint256(in) {}
};

// ── CPQPubKey ────────────────────────────────────────────────────────────────

/** An encapsulated ML-DSA-65 public key (1,952 bytes). */
class CPQPubKey
{
public:
    static constexpr unsigned int SIZE           = PQ_PUBKEY_SIZE;
    static constexpr unsigned int SIGNATURE_SIZE = PQ_SIGNATURE_SIZE;

    /** Magic header byte: 0xPQ = 0x50. Stored as first byte for type detection. */
    static constexpr unsigned char HEADER = 0x50;

private:
    /** The raw ML-DSA-65 public key. */
    std::array<unsigned char, SIZE> m_data;
    bool m_valid{false};

public:
    //! Construct an invalid public key.
    CPQPubKey() : m_valid(false)
    {
        m_data.fill(0);
    }

    //! Construct from raw bytes.
    CPQPubKey(const unsigned char* pbegin, const unsigned char* pend)
    {
        Set(pbegin, pend);
    }

    //! Construct from a span.
    explicit CPQPubKey(std::span<const unsigned char> data)
    {
        Set(data.data(), data.data() + data.size());
    }

    //! Set from raw bytes.
    void Set(const unsigned char* pbegin, const unsigned char* pend)
    {
        if (static_cast<size_t>(pend - pbegin) == SIZE) {
            std::memcpy(m_data.data(), pbegin, SIZE);
            m_valid = true;
        } else {
            m_data.fill(0);
            m_valid = false;
        }
    }

    //! Check validity.
    bool IsValid() const { return m_valid; }

    //! Size is always fixed for PQ keys (no compressed/uncompressed distinction).
    unsigned int size() const { return m_valid ? SIZE : 0; }

    const unsigned char* data() const { return m_data.data(); }
    const unsigned char* begin() const { return m_data.data(); }
    const unsigned char* end() const { return m_data.data() + size(); }

    //! Get the SHA-256 hash of this public key (used for address derivation).
    CPQKeyID GetID() const
    {
        uint256 hash;
        CSHA256().Write(m_data.data(), SIZE).Finalize(hash.data());
        return CPQKeyID(hash);
    }

    //! Verify an ML-DSA-65 signature against this public key.
    bool Verify(const uint256& hash, const std::vector<unsigned char>& vchSig) const;

    //! Verify a signature over arbitrary-length message bytes.
    bool VerifyMessage(std::span<const unsigned char> msg,
                       std::span<const unsigned char> sig) const;

    //! Comparison operators.
    friend bool operator==(const CPQPubKey& a, const CPQPubKey& b)
    {
        return a.m_valid == b.m_valid && a.m_data == b.m_data;
    }
    friend bool operator!=(const CPQPubKey& a, const CPQPubKey& b)
    {
        return !(a == b);
    }
    friend bool operator<(const CPQPubKey& a, const CPQPubKey& b)
    {
        return a.m_data < b.m_data;
    }

    //! Serialization (for network and disk).
    template <typename Stream>
    void Serialize(Stream& s) const
    {
        unsigned int len = size();
        ::WriteCompactSize(s, len);
        s << std::span{m_data.data(), len};
    }

    template <typename Stream>
    void Unserialize(Stream& s)
    {
        unsigned int len = static_cast<unsigned int>(::ReadCompactSize(s));
        if (len == SIZE) {
            s >> std::span{m_data.data(), SIZE};
            m_valid = true;
        } else {
            // Skip invalid data
            std::vector<unsigned char> tmp(len);
            if (len > 0) s >> std::span{tmp.data(), len};
            m_data.fill(0);
            m_valid = false;
        }
    }
};

// ── CPQKey ───────────────────────────────────────────────────────────────────

/** An encapsulated ML-DSA-65 private key (4,032 bytes).
 *
 *  Uses secure_allocator to prevent key material from being swapped to disk.
 *  No compression flag — PQ keys have a single canonical form.
 */
class CPQKey
{
public:
    static constexpr unsigned int SECRETKEY_SIZE = PQ_SECRETKEY_SIZE;
    static constexpr unsigned int PUBKEY_SIZE    = PQ_PUBKEY_SIZE;

private:
    using KeyType = std::array<unsigned char, SECRETKEY_SIZE>;

    //! The actual secret key data. nullptr for invalid keys.
    secure_unique_ptr<KeyType> m_keydata;

    void MakeKeyData()
    {
        if (!m_keydata) m_keydata = make_secure_unique<KeyType>();
    }

    void ClearKeyData()
    {
        m_keydata.reset();
    }

public:
    CPQKey() noexcept = default;
    CPQKey(CPQKey&&) noexcept = default;
    CPQKey& operator=(CPQKey&&) noexcept = default;

    CPQKey& operator=(const CPQKey& other)
    {
        if (this != &other) {
            if (other.m_keydata) {
                MakeKeyData();
                *m_keydata = *other.m_keydata;
            } else {
                ClearKeyData();
            }
        }
        return *this;
    }

    CPQKey(const CPQKey& other) { *this = other; }

    friend bool operator==(const CPQKey& a, const CPQKey& b)
    {
        return a.size() == b.size() &&
               std::memcmp(a.data(), b.data(), a.size()) == 0;
    }

    //! Check whether this private key is valid.
    bool IsValid() const { return !!m_keydata; }

    unsigned int size() const { return m_keydata ? SECRETKEY_SIZE : 0; }
    const unsigned char* data() const
    {
        return m_keydata ? m_keydata->data() : nullptr;
    }

    //! Generate a new ML-DSA-65 keypair using CSPRNG.
    //! Populates this key with the secret key and returns the public key.
    CPQPubKey MakeNewKey();

    //! Compute the public key from this secret key.
    //! ML-DSA-65 secret keys contain the public key — this extracts it.
    CPQPubKey GetPubKey() const;

    //! Sign a 32-byte hash. Returns the signature in vchSig.
    bool Sign(const uint256& hash, std::vector<unsigned char>& vchSig) const;

    //! Sign arbitrary message bytes (ML-DSA-65 signs messages directly, not hashes).
    bool SignMessage(std::span<const unsigned char> msg,
                     std::vector<unsigned char>& vchSig) const;

    //! Verify that this private key corresponds to the given public key.
    bool VerifyPubKey(const CPQPubKey& pubkey) const;

    //! Set from raw secret key bytes.
    void Set(const unsigned char* pbegin, const unsigned char* pend)
    {
        if (static_cast<size_t>(pend - pbegin) == SECRETKEY_SIZE) {
            MakeKeyData();
            std::memcpy(m_keydata->data(), pbegin, SECRETKEY_SIZE);
        } else {
            ClearKeyData();
        }
    }
};

/** Generate a new random ML-DSA-65 key pair. */
CPQKey GenerateRandomPQKey() noexcept;

#endif // NEX_PQKEY_H

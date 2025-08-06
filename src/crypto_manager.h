#ifndef CRYPTO_MANAGER_H
#define CRYPTO_MANAGER_H

#include "utils.h"
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <random>

// Crypto constants
#define AES_KEY_SIZE 32      // AES-256
#define AES_IV_SIZE 16       // AES block size
#define AUTH_KEY_SIZE 64     // Pre-shared key size
#define HMAC_SIZE 32         // SHA-256 HMAC size
#define SALT_SIZE 16         // Salt for key derivation

// Packet types
enum class PacketType : uint8_t {
    AUTH_REQUEST = 0x01,
    AUTH_RESPONSE = 0x02,
    AUTH_SUCCESS = 0x03,
    AUTH_FAILED = 0x04,
    DATA_PACKET = 0x10,
    KEEPALIVE = 0x20
};

// Encrypted packet header
struct EncryptedHeader {
    uint8_t packet_type;
    uint8_t reserved[3];
    uint32_t data_length;
    uint8_t iv[AES_IV_SIZE];
    uint8_t hmac[HMAC_SIZE];
} __attribute__((packed));

class CryptoManager {
private:
    bool initialized;
    std::string pre_shared_key;
    
    // Encryption keys
    uint8_t aes_key[AES_KEY_SIZE];
    uint8_t hmac_key[AES_KEY_SIZE];
    
    // Authentication state
    bool authenticated;
    std::chrono::steady_clock::time_point auth_time;
    
    // Random number generator
    std::random_device rd;
    std::mt19937 gen;

public:
    CryptoManager();
    ~CryptoManager();
    
    // Initialize with pre-shared key
    bool initialize(const std::string& psk);
    
    // Key derivation from PSK
    bool derive_keys(const uint8_t* salt, size_t salt_len);
    
    // Authentication protocol
    bool create_auth_request(char* buffer, size_t& buffer_size);
    bool handle_auth_request(const char* buffer, size_t buffer_size, 
                           char* response, size_t& response_size);
    bool handle_auth_response(const char* buffer, size_t buffer_size);
    
    // Encryption/Decryption
    bool encrypt_packet(const char* plaintext, size_t plaintext_size,
                       char* ciphertext, size_t& ciphertext_size);
    bool decrypt_packet(const char* ciphertext, size_t ciphertext_size,
                       char* plaintext, size_t& plaintext_size);
    
    // Packet handling
    bool wrap_data_packet(const char* data, size_t data_size,
                         char* wrapped, size_t& wrapped_size);
    bool unwrap_data_packet(const char* wrapped, size_t wrapped_size,
                           char* data, size_t& data_size);
    
    // Status
    bool is_authenticated() const { return authenticated; }
    bool needs_reauth() const;
    
    // Utilities
    static std::string generate_psk();
    static bool verify_hmac(const uint8_t* data, size_t data_len,
                           const uint8_t* hmac, const uint8_t* key);

private:
    // Internal crypto functions
    bool generate_iv(uint8_t* iv);
    bool compute_hmac(const uint8_t* data, size_t data_len, 
                     const uint8_t* key, uint8_t* hmac);
    bool constant_time_compare(const uint8_t* a, const uint8_t* b, size_t len);
    
    // Key derivation (PBKDF2)
    bool pbkdf2(const uint8_t* password, size_t password_len,
               const uint8_t* salt, size_t salt_len,
               int iterations, uint8_t* key, size_t key_len);
};

#endif // CRYPTO_MANAGER_H

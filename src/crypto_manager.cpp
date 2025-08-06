#include "crypto_manager.h"
#include <openssl/kdf.h>
#include <openssl/hmac.h>
#include <cstring>

CryptoManager::CryptoManager() : initialized(false), authenticated(false), gen(rd()) {
    memset(aes_key, 0, sizeof(aes_key));
    memset(hmac_key, 0, sizeof(hmac_key));
}

CryptoManager::~CryptoManager() {
    // Clear sensitive data
    memset(aes_key, 0, sizeof(aes_key));
    memset(hmac_key, 0, sizeof(hmac_key));
}

bool CryptoManager::initialize(const std::string& psk) {
    if (psk.length() < 16) {
        Logger::log(LogLevel::ERROR, "Pre-shared key too short (minimum 16 characters)");
        return false;
    }
    
    pre_shared_key = psk;
    initialized = true;
    authenticated = false;
    
    Logger::log(LogLevel::INFO, "Crypto manager initialized with PSK");
    return true;
}

bool CryptoManager::derive_keys(const uint8_t* salt, size_t salt_len) {
    if (!initialized) {
        Logger::log(LogLevel::ERROR, "Crypto manager not initialized");
        return false;
    }
    
    // Derive AES key
    if (!pbkdf2((const uint8_t*)pre_shared_key.c_str(), pre_shared_key.length(),
               salt, salt_len, 10000, aes_key, AES_KEY_SIZE)) {
        Logger::log(LogLevel::ERROR, "Failed to derive AES key");
        return false;
    }
    
    // Derive HMAC key (using different salt)
    uint8_t hmac_salt[SALT_SIZE];
    memcpy(hmac_salt, salt, salt_len);
    for (size_t i = 0; i < salt_len; i++) {
        hmac_salt[i] ^= 0xAA; // XOR with pattern to create different salt
    }
    
    if (!pbkdf2((const uint8_t*)pre_shared_key.c_str(), pre_shared_key.length(),
               hmac_salt, salt_len, 10000, hmac_key, AES_KEY_SIZE)) {
        Logger::log(LogLevel::ERROR, "Failed to derive HMAC key");
        return false;
    }
    
    Logger::log(LogLevel::DEBUG, "Encryption keys derived successfully");
    return true;
}

bool CryptoManager::create_auth_request(char* buffer, size_t& buffer_size) {
    if (!initialized) {
        return false;
    }
    
    size_t required_size = sizeof(EncryptedHeader) + SALT_SIZE;
    if (buffer_size < required_size) {
        buffer_size = required_size;
        return false;
    }
    
    EncryptedHeader* header = (EncryptedHeader*)buffer;
    header->packet_type = (uint8_t)PacketType::AUTH_REQUEST;
    memset(header->reserved, 0, sizeof(header->reserved));
    header->data_length = htonl(SALT_SIZE);
    
    // Generate salt for key derivation
    uint8_t* salt = (uint8_t*)(buffer + sizeof(EncryptedHeader));
    if (!RAND_bytes(salt, SALT_SIZE)) {
        Logger::log(LogLevel::ERROR, "Failed to generate random salt");
        return false;
    }
    
    // Derive keys using this salt
    if (!derive_keys(salt, SALT_SIZE)) {
        return false;
    }
    
    // Generate IV and compute HMAC
    if (!generate_iv(header->iv)) {
        return false;
    }
    
    if (!compute_hmac(salt, SALT_SIZE, hmac_key, header->hmac)) {
        return false;
    }
    
    buffer_size = required_size;
    Logger::log(LogLevel::DEBUG, "Created authentication request");
    return true;
}

bool CryptoManager::handle_auth_request(const char* buffer, size_t buffer_size, 
                                       char* response, size_t& response_size) {
    if (buffer_size < sizeof(EncryptedHeader) + SALT_SIZE) {
        return false;
    }
    
    const EncryptedHeader* header = (const EncryptedHeader*)buffer;
    if (header->packet_type != (uint8_t)PacketType::AUTH_REQUEST) {
        return false;
    }
    
    const uint8_t* salt = (const uint8_t*)(buffer + sizeof(EncryptedHeader));
    
    // Derive keys using received salt
    if (!derive_keys(salt, SALT_SIZE)) {
        return false;
    }
    
    // Verify HMAC
    uint8_t expected_hmac[HMAC_SIZE];
    if (!compute_hmac(salt, SALT_SIZE, hmac_key, expected_hmac)) {
        return false;
    }
    
    if (!constant_time_compare(header->hmac, expected_hmac, HMAC_SIZE)) {
        Logger::log(LogLevel::WARNING, "Authentication failed: HMAC mismatch");
        return false;
    }
    
    // Create success response
    size_t required_size = sizeof(EncryptedHeader);
    if (response_size < required_size) {
        response_size = required_size;
        return false;
    }
    
    EncryptedHeader* resp_header = (EncryptedHeader*)response;
    resp_header->packet_type = (uint8_t)PacketType::AUTH_SUCCESS;
    memset(resp_header->reserved, 0, sizeof(resp_header->reserved));
    resp_header->data_length = 0;
    
    if (!generate_iv(resp_header->iv)) {
        return false;
    }
    
    // HMAC of empty data for success message
    uint8_t empty_data = 0;
    if (!compute_hmac(&empty_data, 0, hmac_key, resp_header->hmac)) {
        return false;
    }
    
    authenticated = true;
    auth_time = std::chrono::steady_clock::now();
    response_size = required_size;
    
    Logger::log(LogLevel::INFO, "Authentication successful (server)");
    return true;
}

bool CryptoManager::handle_auth_response(const char* buffer, size_t buffer_size) {
    if (buffer_size < sizeof(EncryptedHeader)) {
        return false;
    }
    
    const EncryptedHeader* header = (const EncryptedHeader*)buffer;
    if (header->packet_type != (uint8_t)PacketType::AUTH_SUCCESS) {
        Logger::log(LogLevel::WARNING, "Authentication failed");
        return false;
    }
    
    // Verify HMAC
    uint8_t expected_hmac[HMAC_SIZE];
    uint8_t empty_data = 0;
    if (!compute_hmac(&empty_data, 0, hmac_key, expected_hmac)) {
        return false;
    }
    
    if (!constant_time_compare(header->hmac, expected_hmac, HMAC_SIZE)) {
        Logger::log(LogLevel::WARNING, "Authentication failed: HMAC mismatch in response");
        return false;
    }
    
    authenticated = true;
    auth_time = std::chrono::steady_clock::now();
    
    Logger::log(LogLevel::INFO, "Authentication successful (client)");
    return true;
}

bool CryptoManager::encrypt_packet(const char* plaintext, size_t plaintext_size,
                                  char* ciphertext, size_t& ciphertext_size) {
    if (!authenticated) {
        return false;
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return false;
    }
    
    uint8_t iv[AES_IV_SIZE];
    if (!generate_iv(iv)) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, aes_key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    
    int len;
    int ciphertext_len = 0;
    
    // Encrypt the data
    if (EVP_EncryptUpdate(ctx, (unsigned char*)ciphertext + AES_IV_SIZE, &len,
                         (const unsigned char*)plaintext, plaintext_size) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    ciphertext_len = len;
    
    if (EVP_EncryptFinal_ex(ctx, (unsigned char*)ciphertext + AES_IV_SIZE + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    ciphertext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    
    // Prepend IV
    memcpy(ciphertext, iv, AES_IV_SIZE);
    ciphertext_size = AES_IV_SIZE + ciphertext_len;
    
    return true;
}

bool CryptoManager::decrypt_packet(const char* ciphertext, size_t ciphertext_size,
                                  char* plaintext, size_t& plaintext_size) {
    if (!authenticated || ciphertext_size < AES_IV_SIZE) {
        return false;
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return false;
    }
    
    const uint8_t* iv = (const uint8_t*)ciphertext;
    const uint8_t* encrypted_data = (const uint8_t*)ciphertext + AES_IV_SIZE;
    size_t encrypted_size = ciphertext_size - AES_IV_SIZE;
    
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, aes_key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    
    int len;
    int plaintext_len = 0;
    
    if (EVP_DecryptUpdate(ctx, (unsigned char*)plaintext, &len,
                         encrypted_data, encrypted_size) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    plaintext_len = len;
    
    if (EVP_DecryptFinal_ex(ctx, (unsigned char*)plaintext + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    plaintext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    plaintext_size = plaintext_len;
    
    return true;
}

bool CryptoManager::wrap_data_packet(const char* data, size_t data_size,
                                    char* wrapped, size_t& wrapped_size) {
    if (!authenticated) {
        return false;
    }
    
    size_t encrypted_size = data_size + AES_BLOCK_SIZE; // Extra space for padding
    size_t required_size = sizeof(EncryptedHeader) + encrypted_size;
    
    if (wrapped_size < required_size) {
        wrapped_size = required_size;
        return false;
    }
    
    EncryptedHeader* header = (EncryptedHeader*)wrapped;
    header->packet_type = (uint8_t)PacketType::DATA_PACKET;
    memset(header->reserved, 0, sizeof(header->reserved));
    
    // Encrypt the data
    char* encrypted_data = wrapped + sizeof(EncryptedHeader);
    size_t actual_encrypted_size = encrypted_size;
    
    if (!encrypt_packet(data, data_size, encrypted_data, actual_encrypted_size)) {
        return false;
    }
    
    header->data_length = htonl(actual_encrypted_size);
    
    // Generate IV for header
    if (!generate_iv(header->iv)) {
        return false;
    }
    
    // Compute HMAC over encrypted data
    if (!compute_hmac((const uint8_t*)encrypted_data, actual_encrypted_size, 
                     hmac_key, header->hmac)) {
        return false;
    }
    
    wrapped_size = sizeof(EncryptedHeader) + actual_encrypted_size;
    return true;
}

bool CryptoManager::unwrap_data_packet(const char* wrapped, size_t wrapped_size,
                                      char* data, size_t& data_size) {
    if (!authenticated || wrapped_size < sizeof(EncryptedHeader)) {
        return false;
    }
    
    const EncryptedHeader* header = (const EncryptedHeader*)wrapped;
    if (header->packet_type != (uint8_t)PacketType::DATA_PACKET) {
        return false;
    }
    
    uint32_t encrypted_size = ntohl(header->data_length);
    if (wrapped_size != sizeof(EncryptedHeader) + encrypted_size) {
        return false;
    }
    
    const char* encrypted_data = wrapped + sizeof(EncryptedHeader);
    
    // Verify HMAC
    uint8_t expected_hmac[HMAC_SIZE];
    if (!compute_hmac((const uint8_t*)encrypted_data, encrypted_size, 
                     hmac_key, expected_hmac)) {
        return false;
    }
    
    if (!constant_time_compare(header->hmac, expected_hmac, HMAC_SIZE)) {
        Logger::log(LogLevel::WARNING, "HMAC verification failed for data packet");
        return false;
    }
    
    // Decrypt the data
    return decrypt_packet(encrypted_data, encrypted_size, data, data_size);
}

bool CryptoManager::needs_reauth() const {
    if (!authenticated) {
        return true;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - auth_time);
    return duration.count() > 60; // Re-authenticate every hour
}

std::string CryptoManager::generate_psk() {
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string psk;
    psk.reserve(64);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    for (int i = 0; i < 64; ++i) {
        psk += charset[dis(gen)];
    }
    
    return psk;
}

bool CryptoManager::generate_iv(uint8_t* iv) {
    return RAND_bytes(iv, AES_IV_SIZE) == 1;
}

bool CryptoManager::compute_hmac(const uint8_t* data, size_t data_len, 
                                const uint8_t* key, uint8_t* hmac) {
    unsigned int hmac_len;
    return HMAC(EVP_sha256(), key, AES_KEY_SIZE, data, data_len, hmac, &hmac_len) != NULL;
}

bool CryptoManager::constant_time_compare(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t result = 0;
    for (size_t i = 0; i < len; i++) {
        result |= a[i] ^ b[i];
    }
    return result == 0;
}

bool CryptoManager::pbkdf2(const uint8_t* password, size_t password_len,
                          const uint8_t* salt, size_t salt_len,
                          int iterations, uint8_t* key, size_t key_len) {
    return PKCS5_PBKDF2_HMAC((const char*)password, password_len,
                            salt, salt_len, iterations,
                            EVP_sha256(), key_len, key) == 1;
}

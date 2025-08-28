// authentication_service.cpp
#include "execution/authentication_service.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <vector>
#include <stdexcept>
#include <iostream>

namespace trading {
namespace execution {

namespace {
// Constants for encryption
constexpr size_t KEY_SIZE = 32;  // 256 bits
constexpr size_t IV_SIZE = 16;   // 128 bits
constexpr size_t SALT_SIZE = 16;

// Helper function for encryption
std::vector<uint8_t> encrypt(const std::string& plaintext, const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create encryption context");
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize encryption");
    }

    std::vector<uint8_t> ciphertext(plaintext.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
    int len = 0;
    int ciphertext_len = 0;

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, 
                         reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed during encryption update");
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize encryption");
    }
    ciphertext_len += len;
    ciphertext.resize(ciphertext_len);

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
}

// Helper function for decryption
std::string decrypt(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create decryption context");
    }

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize decryption");
    }

    std::vector<uint8_t> plaintext(ciphertext.size());
    int len = 0;
    int plaintext_len = 0;

    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed during decryption update");
    }
    plaintext_len = len;

    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize decryption");
    }
    plaintext_len += len;
    plaintext.resize(plaintext_len);

    EVP_CIPHER_CTX_free(ctx);
    return std::string(plaintext.begin(), plaintext.end());
}

// Derive key from master key and salt
std::vector<uint8_t> deriveKey(const std::string& masterKey, const std::vector<uint8_t>& salt) {
    std::vector<uint8_t> derivedKey(KEY_SIZE);
    if (PKCS5_PBKDF2_HMAC(masterKey.data(), masterKey.size(), 
                          salt.data(), salt.size(), 
                          10000, EVP_sha256(), 
                          KEY_SIZE, derivedKey.data()) != 1) {
        throw std::runtime_error("Failed to derive key");
    }
    return derivedKey;
}
} // anonymous namespace

AuthenticationService::AuthenticationService() {
    // Initialize OpenSSL
    OpenSSL_add_all_algorithms();
}

AuthenticationService::~AuthenticationService() {
    // Clean up OpenSSL
    EVP_cleanup();
}

std::optional<ExchangeCredentials> AuthenticationService::getCredentials(const core::ExchangeId& exchangeId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = credentials_.find(exchangeId.toString());
    if (it != credentials_.end()) {
        return decryptCredentials(it->second);
    }
    return std::nullopt;
}

void AuthenticationService::setCredentials(const core::ExchangeId& exchangeId, const ExchangeCredentials& credentials) {
    std::lock_guard<std::mutex> lock(mutex_);
    credentials_[exchangeId.toString()] = encryptCredentials(credentials);
}

void AuthenticationService::removeCredentials(const core::ExchangeId& exchangeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    credentials_.erase(exchangeId.toString());
}

bool AuthenticationService::hasCredentials(const core::ExchangeId& exchangeId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return credentials_.find(exchangeId.toString()) != credentials_.end();
}

bool AuthenticationService::validateCredentials(const core::ExchangeId& exchangeId, const ExchangeCredentials& credentials) {
    // This would typically call the exchange API to validate credentials
    // For now, we'll just implement a basic placeholder
    try {
        // Construct a simple validation client to test credentials
        // In a real implementation, this would call the exchange API
        // to verify that the credentials work
        
        // Placeholder for credential validation logic
        // In reality, you would check:
        // 1. That the credentials format is valid for the exchange
        // 2. That the credentials can successfully authenticate with the exchange
        
        // For testing purposes, let's just validate format requirements
        bool validFormat = 
            !credentials.apiKey.empty() && 
            !credentials.apiSecret.empty() && 
            credentials.apiKey.length() >= 16;
            
        if (validFormat) {
            // Store the valid credentials
            setCredentials(exchangeId, credentials);
            return true;
        }
        
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Credential validation failed: " << e.what() << std::endl;
        return false;
    }
}

ExchangeCredentials AuthenticationService::encryptCredentials(const ExchangeCredentials& credentials) const {
    try {
        // In a real-world application, you would:
        // 1. Retrieve a master key from a secure source (HSM, KMS, etc.)
        // 2. Generate random salt and IV for each encryption
        // 3. Derive an encryption key from the master key and salt
        // 4. Encrypt the credentials
        // 5. Store the salt, IV, and encrypted data
        
        // This is a simplified implementation for demonstration purposes
        
        // Generate random salt and IV
        std::vector<uint8_t> salt(SALT_SIZE);
        std::vector<uint8_t> iv(IV_SIZE);
        if (RAND_bytes(salt.data(), salt.size()) != 1 ||
            RAND_bytes(iv.data(), iv.size()) != 1) {
            throw std::runtime_error("Failed to generate random data");
        }
        
        // For demonstration, using a hardcoded master key
        // In production, this would be retrieved from a secure source
        std::string masterKey = "this_would_be_retrieved_from_secure_storage";
        
        // Derive key from master key and salt
        std::vector<uint8_t> key = deriveKey(masterKey, salt);
        
        // Encrypt API key and secret
        std::string plaintext = credentials.apiKey + "|" + credentials.apiSecret;
        std::vector<uint8_t> ciphertext = encrypt(plaintext, key, iv);
        
        // Create encrypted credentials
        ExchangeCredentials encryptedCredentials;
        
        // Store salt and IV as part of the encrypted credentials
        // In a real implementation, you might store these separately
        encryptedCredentials.apiKey = std::string(salt.begin(), salt.end()) + 
                                     std::string(iv.begin(), iv.end());
        
        // Store the encrypted data
        encryptedCredentials.apiSecret = std::string(ciphertext.begin(), ciphertext.end());
        
        return encryptedCredentials;
    } catch (const std::exception& e) {
        std::cerr << "Encryption failed: " << e.what() << std::endl;
        // Return empty credentials to indicate failure
        return ExchangeCredentials{};
    }
}

ExchangeCredentials AuthenticationService::decryptCredentials(const ExchangeCredentials& encryptedCredentials) const {
    try {
        // Extract salt and IV from the stored data
        if (encryptedCredentials.apiKey.length() < SALT_SIZE + IV_SIZE) {
            throw std::runtime_error("Invalid encrypted credentials format");
        }
        
        std::vector<uint8_t> salt(encryptedCredentials.apiKey.begin(), 
                                 encryptedCredentials.apiKey.begin() + SALT_SIZE);
        std::vector<uint8_t> iv(encryptedCredentials.apiKey.begin() + SALT_SIZE, 
                               encryptedCredentials.apiKey.begin() + SALT_SIZE + IV_SIZE);
        
        // For demonstration, using a hardcoded master key
        // In production, this would be retrieved from a secure source
        std::string masterKey = "this_would_be_retrieved_from_secure_storage";
        
        // Derive key from master key and salt
        std::vector<uint8_t> key = deriveKey(masterKey, salt);
        
        // Extract ciphertext from the stored data
        std::vector<uint8_t> ciphertext(encryptedCredentials.apiSecret.begin(), 
                                      encryptedCredentials.apiSecret.end());
        
        // Decrypt the data
        std::string plaintext = decrypt(ciphertext, key, iv);
        
        // Parse the decrypted data
        size_t separatorPos = plaintext.find('|');
        if (separatorPos == std::string::npos) {
            throw std::runtime_error("Invalid decrypted data format");
        }
        
        ExchangeCredentials decryptedCredentials;
        decryptedCredentials.apiKey = plaintext.substr(0, separatorPos);
        decryptedCredentials.apiSecret = plaintext.substr(separatorPos + 1);
        
        return decryptedCredentials;
    } catch (const std::exception& e) {
        std::cerr << "Decryption failed: " << e.what() << std::endl;
        // Return empty credentials to indicate failure
        return ExchangeCredentials{};
    }
}

} // namespace execution
} // namespace trading
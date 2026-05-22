#pragma once
#include <windows.h>
#include <bcrypt.h>

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#pragma comment(lib, "bcrypt.lib")

std::string BytesToHex(const std::vector<unsigned char>& data)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (unsigned char b : data)
        oss << std::setw(2) << static_cast<int>(b);

    return oss.str();
}

std::string CalcFileSHA256(const std::wstring& filePath)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;

    DWORD cbData = 0;
    DWORD cbHash = 0;
    DWORD cbHashObject = 0;

    std::vector<unsigned char> hashObject;
    std::vector<unsigned char> hash;

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &hAlg,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        0
    );

    if (status < 0)
        throw std::runtime_error("[CalcFileSHA256] BCryptOpenAlgorithmProvider failed");

    status = BCryptGetProperty(
        hAlg,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&cbHashObject),
        sizeof(DWORD),
        &cbData,
        0
    );

    if (status < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("[CalcFileSHA256] BCryptGetProperty BCRYPT_OBJECT_LENGTH failed");
    }

    status = BCryptGetProperty(
        hAlg,
        BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&cbHash),
        sizeof(DWORD),
        &cbData,
        0
    );

    if (status < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("[CalcFileSHA256] BCryptGetProperty BCRYPT_HASH_LENGTH failed");
    }

    hashObject.resize(cbHashObject);
    hash.resize(cbHash);

    status = BCryptCreateHash(
        hAlg,
        &hHash,
        hashObject.data(),
        cbHashObject,
        nullptr,
        0,
        0
    );

    if (status < 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("[CalcFileSHA256] BCryptCreateHash failed");
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file)
    {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("[CalcFileSHA256] Cannot open file");
    }

    const size_t bufferSize = 1024 * 1024; // 1 MB
    std::vector<char> buffer(bufferSize);

    while (file)
    {
        file.read(buffer.data(), buffer.size());
        std::streamsize bytesRead = file.gcount();

        if (bytesRead > 0)
        {
            status = BCryptHashData(
                hHash,
                reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(bytesRead),
                0
            );

            if (status < 0)
            {
                BCryptDestroyHash(hHash);
                BCryptCloseAlgorithmProvider(hAlg, 0);
                throw std::runtime_error("[CalcFileSHA256] BCryptHashData failed");
            }
        }
    }

    status = BCryptFinishHash(
        hHash,
        hash.data(),
        cbHash,
        0
    );

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (status < 0)
        throw std::runtime_error("[CalcFileSHA256] BCryptFinishHash failed");

    return BytesToHex(hash);
}
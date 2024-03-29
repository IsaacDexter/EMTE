#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h> // For HRESULT
#include <exception> // For std::exception
#include <iostream> //for std::cerr


inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw std::exception();
    }
}

inline void ThrowIfFailed(HRESULT hr, const char* errorMessage)
{
    if (FAILED(hr))
    {
        std::cerr << errorMessage;
        throw std::exception();
    }
}
#include "network/HttpResponse.h"
#include <zlib.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#if !defined(__EMSCRIPTEN__)

namespace ax {
namespace network {

// Helper function to check if the response is compressed and decompress it
bool HttpResponse::decompressResponseData() {
    // First, check if the request had Accept-Encoding header indicating compression support
    bool requestAcceptsCompression = false;
    const auto& requestHeaders = _pHttpRequest->getHeaders();
    for (const auto& header : requestHeaders) {
        std::string lowerHeader = header;
        std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(), ::tolower);
        if (lowerHeader.find("accept-encoding:") != std::string::npos) {
            // Check if it contains gzip or deflate
            if (lowerHeader.find("gzip") != std::string::npos || 
                lowerHeader.find("deflate") != std::string::npos) {
                requestAcceptsCompression = true;
                break;
            }
        }
    }
    
    // If request doesn't accept compression, don't try to decompress
    if (!requestAcceptsCompression) {
        return true;
    }
    
    // Check response Content-Encoding header
    auto encodingIter = _responseHeaders.find("content-encoding");
    if (encodingIter == _responseHeaders.end()) {
        // No compression used, nothing to do
        return true;
    }
    
    std::string encoding = encodingIter->second;
    std::transform(encoding.begin(), encoding.end(), encoding.begin(), ::tolower);
    
    // Remove whitespace
    encoding.erase(std::remove_if(encoding.begin(), encoding.end(), ::isspace), encoding.end());
    
    bool decompressionSuccess = false;
    if (encoding == "gzip") {
        decompressionSuccess = decompressGzip();
    } else if (encoding == "deflate") {
        decompressionSuccess = decompressDeflate();
    }
    
    // If decompression was successful, remove the content-encoding header
    if (decompressionSuccess) {
        _responseHeaders.erase("content-encoding");
    }
    
    return decompressionSuccess;
}

// Gzip decompression implementation
bool HttpResponse::decompressGzip() {
    // Prepare for decompression
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    
    // Use MAX_WBITS | 16 for gzip format (automatically handles header)
    if (inflateInit2(&strm, MAX_WBITS | 16) != Z_OK) {
        return false;
    }
    
    // Set input buffer
    strm.next_in = reinterpret_cast<unsigned char*>(_responseData.data());
    strm.avail_in = _responseData.size();
    
    // Allocate output buffer (start with 4x the input size as estimate)
    size_t outputSize = _responseData.size() * 4;
    yasio::sbyte_buffer decompressedData;
    decompressedData.resize(outputSize);
    
    strm.next_out = reinterpret_cast<unsigned char*>(decompressedData.data());
    strm.avail_out = decompressedData.size();
    
    // Decompress
    int ret = inflate(&strm, Z_NO_FLUSH);
    
    // If output buffer was too small, expand it and continue
    while (ret == Z_OK && strm.avail_out == 0) {
        outputSize *= 2;
        decompressedData.resize(outputSize);
        strm.next_out = reinterpret_cast<unsigned char*>(decompressedData.data() + strm.total_out);
        strm.avail_out = decompressedData.size() - strm.total_out;
        ret = inflate(&strm, Z_NO_FLUSH);
    }
    
    if (ret != Z_STREAM_END) {
        inflateEnd(&strm);
        return false;
    }
    
    // Resize to actual decompressed size
    decompressedData.resize(strm.total_out);
    
    // Clean up
    inflateEnd(&strm);
    
    // Replace response data with decompressed data
    _responseData.swap(decompressedData);
    
    return true;
}

// Deflate decompression implementation
bool HttpResponse::decompressDeflate() {
    // Prepare for decompression
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    
    // Try raw deflate format first
    int ret = inflateInit2(&strm, -MAX_WBITS);
    if (ret != Z_OK) {
        // If raw deflate fails, try with zlib header format
        inflateEnd(&strm);
        memset(&strm, 0, sizeof(strm));
        if (inflateInit(&strm) != Z_OK) {
            return false;
        }
    }
    
    // Set input buffer
    strm.next_in = reinterpret_cast<unsigned char*>(_responseData.data());
    strm.avail_in = _responseData.size();
    
    // Allocate output buffer (start with 4x the input size as estimate)
    size_t outputSize = _responseData.size() * 4;
    yasio::sbyte_buffer decompressedData;
    decompressedData.resize(outputSize);
    
    strm.next_out = reinterpret_cast<unsigned char*>(decompressedData.data());
    strm.avail_out = decompressedData.size();
    
    // Decompress
    ret = inflate(&strm, Z_NO_FLUSH);
    
    // If output buffer was too small, expand it and continue
    while (ret == Z_OK && strm.avail_out == 0) {
        outputSize *= 2;
        decompressedData.resize(outputSize);
        strm.next_out = reinterpret_cast<unsigned char*>(decompressedData.data() + strm.total_out);
        strm.avail_out = decompressedData.size() - strm.total_out;
        ret = inflate(&strm, Z_NO_FLUSH);
    }
    
    if (ret != Z_STREAM_END) {
        inflateEnd(&strm);
        return false;
    }
    
    // Resize to actual decompressed size
    decompressedData.resize(strm.total_out);
    
    // Clean up
    inflateEnd(&strm);
    
    // Replace response data with decompressed data
    _responseData.swap(decompressedData);
    
    return true;
}

} // namespace network
} // namespace ax

#endif // !defined(__EMSCRIPTEN__)
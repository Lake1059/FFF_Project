// jpeg_gainmap_probe.cpp — standalone probe: dump JpegGainMapHit as JSON.
// Build: build_jpeg_probe.bat (cl, no external libs).
// Usage: jpeg_gainmap_probe.exe <file.jpg|file.webp> [...]
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../3FP/Core/GainMapJpeg.h"

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string ToHex(const uint8_t* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s; s.resize(n*2);
    for (size_t i=0;i<n;i++){ s[i*2]=H[p[i]>>4]; s[i*2+1]=H[p[i]&15]; }
    return s;
}

void Sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xabc5ed5b,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf4e5346f,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t H[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint64_t bitLen = (uint64_t)len * 8;
    std::vector<uint8_t> m(data, data+len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i=7;i>=0;i--) m.push_back((uint8_t)(bitLen >> (i*8)));
    for (size_t off=0; off<m.size(); off+=64) {
        uint32_t w[64];
        for (int i=0;i<16;i++) w[i]=(uint32_t(m[off+i*4])<<24)|(uint32_t(m[off+i*4+1])<<16)|(uint32_t(m[off+i*4+2])<<8)|m[off+i*4+3];
        for (int i=16;i<64;i++) {
            uint32_t s1=(w[i-15]>>7|w[i-15]<<25)^(w[i-15]>>18|w[i-15]<<14)^(w[i-15]>>3);
            uint32_t s2=(w[i-2]>>17|w[i-2]<<15)^(w[i-2]>>19|w[i-2]<<13)^(w[i-2]>>10);
            w[i]=w[i-16]+s1+w[i-7]+s2;
        }
        uint32_t a=H[0],b=H[1],c=H[2],cc=H[3],d=H[4],e=H[5],f=H[6],g=H[7];
        for (int i=0;i<64;i++) {
            uint32_t S1=(e>>6|e<<26)^(e>>11|e<<21)^(e>>25|e<<7);
            uint32_t ch=(e&f)^(~e&g);
            uint32_t t1=g+S1+ch+K[i]+w[i];
            uint32_t S0=(a>>2|a<<30)^(a>>13|a<<19)^(a>>22|a<<10);
            uint32_t maj=(a&b)^(a&cc)^(b&cc);
            uint32_t t2=S0+maj;
            g=f;f=e;e=cc+t1;cc=d;d=c;c=b;b=a;a=t1+t2;
        }
        H[0]+=a;H[1]+=b;H[2]+=c;H[3]+=cc;H[4]+=d;H[5]+=e;H[6]+=f;H[7]+=g;
    }
    for (int i=0;i<8;i++){
        out[i*4+0]=(uint8_t)(H[i]>>24);out[i*4+1]=(uint8_t)(H[i]>>16);
        out[i*4+2]=(uint8_t)(H[i]>>8); out[i*4+3]=(uint8_t)(H[i]);
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { wprintf(L"usage: jpeg_gainmap_probe.exe <file> [...]\n"); return 1; }
    for (int i = 1; i < argc; i++) {
        fp3::JpegGainMapHit h;
        fp3::ProbeJpegGainMap(argv[i], h);
        char pathA[1024] = {0};
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, pathA, sizeof(pathA)-1, nullptr, nullptr);
        uint8_t sha[32] = {0};
        Sha256(h.gainBytes.data(), h.gainBytes.size(), sha);
        printf("{\n");
        printf("  \"file\": \"%s\",\n", pathA);
        printf("  \"found\": %s,\n", h.found ? "true" : "false");
        printf("  \"containerKind\": \"%s\",\n", h.containerKind.c_str());
        printf("  \"family\": \"%s\",\n", h.family.c_str());
        printf("  \"metaSource\": \"%s\",\n", h.metaSource.c_str());
        printf("  \"gainCodec\": \"%s\",\n", h.gainCodec.c_str());
        printf("  \"baseBytes\": %zu,\n", h.baseBytes.size());
        printf("  \"gainBytes\": %zu,\n", h.gainBytes.size());
        printf("  \"gainSha256\": \"%s\",\n", ToHex(sha,32).c_str());
        printf("  \"offsetSdr\": [%g,%g,%g],\n", h.offsetSdr[0],h.offsetSdr[1],h.offsetSdr[2]);
        printf("  \"offsetHdr\": [%g,%g,%g],\n", h.offsetHdr[0],h.offsetHdr[1],h.offsetHdr[2]);
        printf("  \"logBoost\": [%g,%g,%g,%g,%g,%g],\n",
            h.logBoost[0],h.logBoost[1],h.logBoost[2],h.logBoost[3],h.logBoost[4],h.logBoost[5]);
        printf("  \"headroom\": %g,\n", h.headroom);
        printf("  \"gamma\": %g,\n", h.gamma);
        printf("  \"reason\": \"%s\"\n", h.reason.c_str());
        printf("}\n");
    }
    return 0;
}

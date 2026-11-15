// gainmap_probe.cpp — command-line probe for private HDR gainmap stills.
#define _CRT_SECURE_NO_WARNINGS
#include "../3FP/Core/GainMapProbe.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>

// ── minimal SHA-256 ─────────────────────────────────────────────────────────
struct Sha256 {
    uint32_t h[8]; uint64_t total; uint8_t buf[64]; size_t buflen;
    static uint32_t rotr(uint32_t x, int n){ return (x>>n)|(x<<(32-n)); }
    void init(){ h[0]=0x6a09e667;h[1]=0xbb67ae85;h[2]=0x3c6ef372;h[3]=0xa54ff53a;
                 h[4]=0x510e527f;h[5]=0x9b05688c;h[6]=0x1f83d9ab;h[7]=0x5be0cd19;
                 total=0; buflen=0; }
    void block(const uint8_t* p){
        static const uint32_t K[64]={
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a0,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for (int i=0;i<16;i++) w[i]=(p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
        for (int i=16;i<64;i++){
            uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
            uint32_t s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i=0;i<64;i++){
            uint32_t S1=rotr(e,6)^rotr(e,11)^rotr(e,25);
            uint32_t ch=(e&f)^(~e&g);
            uint32_t t1=hh+S1+ch+K[i]+w[i];
            uint32_t S0=rotr(a,2)^rotr(a,13)^rotr(a,22);
            uint32_t maj=(a&b)^(a&c)^(b&c);
            uint32_t t2=S0+maj;
            hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    void update(const uint8_t* p, size_t n){
        total+=n;
        while(n){ size_t take=64-buflen; if(take>n)take=n;
            memcpy(buf+buflen,p,take); buflen+=take; p+=take; n-=take;
            if(buflen==64){ block(buf); buflen=0; } }
    }
    void final(uint8_t out[32]){
        uint64_t bits=total*8;
        uint8_t pad=0x80; update(&pad,1);
        uint8_t z=0; while(buflen!=56) update(&z,1);
        uint8_t len[8]; for(int i=0;i<8;i++) len[i]=(uint8_t)(bits>>((7-i)*8));
        update(len,8);
        for(int i=0;i<8;i++){ out[i*4]=h[i]>>24; out[i*4+1]=h[i]>>16; out[i*4+2]=h[i]>>8; out[i*4+3]=h[i]; }
    }
};

static std::string JsonEscape(const std::string& s){
    std::string o; o.reserve(s.size()+8);
    for (char c : s){
        if (c=='"') o+="\\\""; else if (c=='\\') o+="\\\\";
        else if (c=='\n') o+="\\n"; else if (c=='\r') o+="\\r"; else if (c=='\t') o+="\\t";
        else if ((unsigned char)c < 0x20) { char b[8]; sprintf(b,"\\u%04x",(unsigned char)c); o+=b; }
        else o+=c;
    }
    return o;
}

static void WritePfm(const std::string& path, const std::vector<float>& rgba, int w, int h){
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "PF\n%d %d\n-1.0\n", w, h);
    // PFM is bottom-up; write top row last. We'll just write top-down (most tools tolerate;
    // note in sidecar).
    for (int y = h-1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            long i = ((long)y*w + x)*4;
            float r = rgba[i], g = rgba[i+1], b = rgba[i+2];
            fwrite(&r, 4, 1, f); fwrite(&g, 4, 1, f); fwrite(&b, 4, 1, f);
        }
    }
    fclose(f);
}

int main(int argc, char** argv){
    if (argc < 2) {
        fprintf(stderr, "usage: gainmap_probe <file> [--dump <dir>] [--base-only]\n");
        return 2;
    }
    std::string path = argv[1];
    std::string dumpDir;
    bool baseOnly = false;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--base-only") baseOnly = true;
        else if (a == "--dump" && i+1 < argc) { dumpDir = argv[++i]; }
    }

    // sha256 of file
    std::string sha;
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (f) {
            Sha256 ctx; ctx.init();
            uint8_t buf[65536]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) ctx.update(buf, n);
            fclose(f);
            uint8_t out[32]; ctx.final(out);
            char b[65]; for (int i = 0; i < 32; i++) sprintf(b+i*2, "%02x", out[i]);
            sha = b;
        }
    }

    GainMapProbeResult R = GainMapProbe::Probe(path, baseOnly);

    // dump
    if (!dumpDir.empty()) {
        if (!R.appliedRgba.empty()) WritePfm(dumpDir + "/applied.pfm", R.appliedRgba, R.outW, R.outH);
        if (!R.baseRgba.empty()) WritePfm(dumpDir + "/base.pfm", R.baseRgba, R.outW, R.outH);
        std::ofstream note(dumpDir + "/note.txt");
        note << "verdict=" << R.verdict << "\nreason=" << R.reason << "\n";
        note << "form=" << R.form << " family=" << R.metaFamily << " source=" << R.metaSource << "\n";
        note << "decode " << R.decodeW << "x" << R.decodeH << " ch=" << R.decodeChannels << " codec=" << R.decodeCodec << "\n";
        note << "apply.ok=" << R.applyOk << " mode=" << R.applyMode << "\n";
        note << "stats p50=" << R.statsP50 << " maxLinear=" << R.statsMaxLinear << " over1Pct=" << R.statsOver1Pct << "\n";
        note << "applied.pfm = linear RGB float after gain apply (top-down in file; PFM convention is bottom-up)\n";
        note << "base.pfm    = linear RGB float SDR base (after sRGB->linear), no gain\n";
    }

    // JSON
    std::ostringstream j;
    j << "{\"schema\":\"3fp-gainmap-probe/1\"";
    j << ",\"file\":\"" << JsonEscape(path) << "\"";
    j << ",\"sha256\":\"" << sha << "\"";
    j << ",\"container\":{\"majorBrand\":\"" << JsonEscape(R.majorBrand) << "\"";
    j << ",\"compatibleBrands\":[";
    for (size_t i = 0; i < R.compatibleBrands.size(); i++) {
        if (i) j << ",";
        j << "\"" << JsonEscape(R.compatibleBrands[i]) << "\"";
    }
    j << "],\"isStill\":" << (R.isStill ? "true" : "false") << "}";
    j << ",\"gainmap\":{\"present\":" << (R.present ? "true" : "false");
    j << ",\"form\":\"" << JsonEscape(R.form) << "\"";
    j << ",\"auxCUrn\":\"" << JsonEscape(R.auxCUrn) << "\"";
    j << ",\"grid\":{\"present\":" << (R.gridPresent ? "true" : "false")
      << ",\"rows\":" << R.gridRows << ",\"cols\":" << R.gridCols
      << ",\"flags\":" << R.gridFlags
      << ",\"tailPresent\":" << (R.gridTailPresent ? "true" : "false")
      << ",\"tailW\":" << R.gridTailW << ",\"tailH\":" << R.gridTailH << "}";
    j << ",\"items\":[";
    for (size_t i = 0; i < R.items.size(); i++) {
        if (i) j << ",";
        j << "{\"id\":" << R.items[i].id << ",\"type\":\"" << JsonEscape(R.items[i].type)
          << "\",\"role\":\"" << JsonEscape(R.items[i].role) << "\"}";
    }
    j << "]}";
    j << ",\"decode\":{\"ok\":" << (R.decodeOk ? "true" : "false")
      << ",\"codec\":\"" << JsonEscape(R.decodeCodec) << "\""
      << ",\"width\":" << R.decodeW << ",\"height\":" << R.decodeH
      << ",\"channels\":" << R.decodeChannels << ",\"bitDepth\":" << R.decodeBitDepth
      << ",\"reason\":\"" << JsonEscape(R.reason.empty() && !R.decodeOk ? "" : "") << "\"}";
    j << ",\"meta\":{\"family\":\"" << JsonEscape(R.metaFamily) << "\""
      << ",\"source\":\"" << JsonEscape(R.metaSource) << "\""
      << ",\"offsetSdr\":[" << R.offsetSdr[0] << "," << R.offsetSdr[1] << "," << R.offsetSdr[2] << "]"
      << ",\"offsetHdr\":[" << R.offsetHdr[0] << "," << R.offsetHdr[1] << "," << R.offsetHdr[2] << "]"
      << ",\"logBoost\":[" << R.logBoostMin[0] << "," << R.logBoostMax[0] << "," << R.logBoostMin[1] << "," << R.logBoostMax[1] << "," << R.logBoostMin[2] << "," << R.logBoostMax[2] << "]"
      << ",\"headroom\":" << R.headroom << ",\"gamma\":" << R.gamma << "}";
    j << ",\"apply\":{\"ok\":" << (R.applyOk ? "true" : "false")
      << ",\"mode\":\"" << JsonEscape(R.applyMode) << "\""
      << ",\"stats\":{\"p50\":" << R.statsP50 << ",\"maxLinear\":" << R.statsMaxLinear
      << ",\"over1Pct\":" << R.statsOver1Pct << "}"
      << ",\"reason\":\"" << JsonEscape(R.reason) << "\"}";
    j << ",\"verdict\":\"" << R.verdict << "\"";
    j << ",\"notes\":[";
    for (size_t i = 0; i < R.notes.size(); i++) {
        if (i) j << ",";
        j << "\"" << JsonEscape(R.notes[i]) << "\"";
    }
    j << "]}";
    printf("%s\n", j.str().c_str());
    return 0;
}

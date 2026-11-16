// apple_meta_probe.cpp — standalone test vector for GainMapAppleMeta.h.
//
// 1) Runs gainmap_apple::RunSelfTest() (in-header math assertions).
// 2) Opens the real iPhone HDR HEIC, parses just enough ISO-BMFF + Exif
//    MakerNote to recover tag33/tag48, the gain-map item size / bit-depth,
//    and the primary image size, then re-derives headroom through the header
//    and asserts it equals the baked-in imazen vector.
//
// Build: build_apple_meta.bat (cl.exe, no ffmpeg linkage needed).
#include "../3FP/Core/GainMapAppleMeta.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using gainmap_apple::AppleHeadroomResult;
using gainmap_apple::ComputeHeadroomFromMakerNote;

namespace {

struct Bytes {
    const uint8_t* p = nullptr;
    size_t n = 0;
    size_t at = 0;
    bool ok = true;
    size_t pos() const { return at; }
    bool need(size_t k) { if (at + k > n) { ok = false; return false; } return true; }
    uint8_t r8() { if (!need(1)) return 0; return p[at++]; }
    uint16_t r16(bool be) { if (!need(2)) return 0; uint16_t v = be ? (uint16_t((p[at]<<8)|p[at+1]))
                                                                 : (uint16_t((p[at])|(p[at+1]<<8))); at+=2; return v; }
    uint32_t r32(bool be) { if (!need(4)) return 0; uint32_t v = be ? ((uint32_t)p[at]<<24)|((uint32_t)p[at+1]<<16)|((uint32_t)p[at+2]<<8)|p[at+3]
                                                                    : ((uint32_t)p[at])|((uint32_t)p[at+1]<<8)|((uint32_t)p[at+2]<<16)|((uint32_t)p[at+3]<<24); at+=4; return v; }
};

struct Box { char type[5]; size_t off; size_t size; size_t hdr; };

// iterate boxes within [start,end)
std::vector<Box> Walk(const uint8_t* d, size_t start, size_t end) {
    std::vector<Box> out;
    size_t o = start;
    while (o + 8 <= end) {
        uint32_t sz = (uint32_t)((d[o]<<24)|(d[o+1]<<16)|(d[o+2]<<8)|d[o+3]);
        Box b; b.off = o; b.hdr = 8;
        std::memcpy(b.type, d+o+4, 4); b.type[4]=0;
        uint64_t size = sz;
        if (sz == 1) { size = ((uint64_t)d[o+8]<<56)|((uint64_t)d[o+9]<<48)|((uint64_t)d[o+10]<<40)|((uint64_t)d[o+11]<<32)
                             |((uint64_t)d[o+12]<<24)|((uint64_t)d[o+13]<<16)|((uint64_t)d[o+14]<<8)|d[o+15]; b.hdr=16; }
        else if (sz == 0) { size = end - o; }
        if (size < b.hdr || o + size > end) break;
        b.size = (size_t)size;
        out.push_back(b);
        o += (size_t)size;
    }
    return out;
}

struct ItemInfo { uint16_t id; char type[5]; std::string ctype; size_t off; size_t len; };
struct Prop { std::string kind; int w=0,h=0; int nch=0; int bits[8]{}; std::string auxType; };

// returns parsed maker-note m33/m48; false on structural failure.
bool ParseMakerNote(const uint8_t* exif, size_t exifLen, float& m33, float& m48,
                    std::string& why) {
    Bytes b{exif, exifLen, 0, true};
    uint32_t tiffOff = b.r32(true);
    size_t t = 4 + tiffOff;
    if (t + 8 > exifLen) { why="tiff hdr out of range"; return false; }
    bool be = exif[t]=='M' && exif[t+1]=='M';
    if (!be && !(exif[t]=='I' && exif[t+1]=='I')) { why="no MM/II"; return false; }
    // magic
    uint16_t magic = be ? uint16_t((exif[t+2]<<8)|exif[t+3]) : uint16_t(exif[t+2]|(exif[t+3]<<8));
    if (magic != 42) { why="magic!=42"; return false; }
    auto r16=[&](size_t o)->uint16_t{ return be?uint16_t((exif[o]<<8)|exif[o+1]):uint16_t(exif[o]|(exif[o+1]<<8)); };
    auto r32=[&](size_t o)->uint32_t{ return be?((uint32_t)exif[o]<<24)|((uint32_t)exif[o+1]<<16)|((uint32_t)exif[o+2]<<8)|exif[o+3]
                                               :((uint32_t)exif[o])|((uint32_t)exif[o+1]<<8)|((uint32_t)exif[o+2]<<16)|((uint32_t)exif[o+3]<<24); };
    uint32_t ifd0 = r32(t+4);
    // IFD0 -> 0x8769
    size_t p = t + ifd0;
    int cnt = r16(p); p += 2;
    long exifIfd = -1;
    for (int i=0;i<cnt;i++,p+=12) {
        uint16_t tag=r16(p), typ=r16(p+2); uint32_t c=r32(p+4);
        if (tag==0x8769 && typ==4 && c>=1) exifIfd = (long)r32(p+8);
    }
    if (exifIfd<0) { why="no ExifIFD"; return false; }
    p = t + exifIfd;
    cnt = r16(p); p+=2;
    long mnAbs=-1; int mnLen=0;
    for (int i=0;i<cnt;i++,p+=12) {
        uint16_t tag=r16(p); uint32_t c=r32(p+4);
        if (tag==0x927C) { mnAbs=(long)(t+r32(p+8)); mnLen=(int)c; }
    }
    if (mnAbs<0 || mnLen<16) { why="no MakerNote"; return false; }
    const char* sig="Apple iOS";
    for (int i=0;i<9;i++) if (exif[mnAbs+i]!=(uint8_t)sig[i]) { why="MN signature"; return false; }
    size_t mnIfd = (size_t)mnAbs + 14;
    cnt = r16(mnIfd); size_t q = mnIfd+2;
    auto readTag=[&](uint16_t want, float& outv)->bool {
        for (int i=0;i<cnt;i++,q+=12) {
            uint16_t tag=r16(q), typ=r16(q+2); uint32_t c=r32(q+4);
            if (tag!=want) continue;
            if (typ==5 || typ==9 || typ==10) { // rational pair (u/u or s/s)
                uint32_t off=r32(q+8); size_t v=(size_t)mnAbs+off;
                if (v+8>exifLen) return false;
                int64_t num = (int64_t)r32(v), den = (int64_t)r32(v+4);
                if (den==0) return false;
                outv = (float)((double)num/(double)den);
                return true;
            }
            if (typ==11) { // FLOAT32 inline
                uint32_t bits=r32(q+8); float f; std::memcpy(&f,&bits,4); outv=f; return true;
            }
            return false;
        }
        return false;
    };
    if (!readTag(0x0021, m33)) { why="tag33"; return false; }
    if (!readTag(0x0030, m48)) { why="tag48"; return false; }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1]
        : "C:\\PLAN\\3FCompare\\testmedia\\gainmap-corpus\\external\\apple_hdr_sample_imazen.heic";

    // ---- 1) in-header self test ----
    std::string report;
    bool stOk = gainmap_apple::RunSelfTest(report);
    std::printf("===== GainMapAppleMeta::RunSelfTest =====\n%s", report.c_str());

    // ---- 2) open and parse the real HEIC ----
    FILE* fp = nullptr; fopen_s(&fp, path.c_str(), "rb");
    if (!fp) { std::printf("[FAIL] cannot open %s\n", path.c_str()); return 2; }
    std::fseek(fp, 0, SEEK_END); long sz = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> d((size_t)sz);
    std::fread(d.data(), 1, (size_t)sz, fp); std::fclose(fp);
    const uint8_t* D = d.data();

    Box meta; bool haveMeta=false;
    for (auto& b : Walk(D, 0, d.size())) if (std::string(b.type)=="meta") { meta=b; haveMeta=true; }
    if (!haveMeta) { std::printf("[FAIL] no meta box\n"); return 2; }

    size_t metaBody = meta.off + meta.hdr + 4;  // skip FullBox version+flags
    uint16_t pitm=0;
    std::vector<ItemInfo> items;
    std::vector<Prop> props;
    std::vector<std::pair<uint16_t,std::vector<int>>> ipma;

    for (auto& b : Walk(D, metaBody, meta.off+meta.size)) {
        std::string t=b.type;
        if (t=="pitm") {
            uint8_t ver=D[b.off+b.hdr];
            pitm = ver==0 ? uint16_t((D[b.off+b.hdr+4]<<8)|D[b.off+b.hdr+5])
                          : uint16_t(((uint32_t)D[b.off+b.hdr+4]<<24)|((uint32_t)D[b.off+b.hdr+5]<<16)|((uint32_t)D[b.off+b.hdr+6]<<8)|D[b.off+b.hdr+7]);
        } else if (t=="iinf") {
            uint8_t ver=D[b.off+b.hdr];
            size_t cstart = b.off+b.hdr+4;
            uint32_t cnt = ver==0 ? uint16_t((D[cstart]<<8)|D[cstart+1])
                                  : ((uint32_t)D[cstart]<<24)|((uint32_t)D[cstart+1]<<16)|((uint32_t)D[cstart+2]<<8)|D[cstart+3];
            size_t p = cstart + (ver==0?2:4);
            for (uint32_t i=0;i<cnt;i++) {
                size_t isize = ((uint32_t)D[p]<<24)|((uint32_t)D[p+1]<<16)|((uint32_t)D[p+2]<<8)|D[p+3];
                if (isize<8) break;
                size_t ib=p+8; uint8_t iver=D[ib]; size_t q=ib+4;
                ItemInfo it;
                uint16_t id = uint16_t((D[q]<<8)|D[q+1]); q+=2;
                uint16_t prot = uint16_t((D[q]<<8)|D[q+1]); q+=2; (void)prot;
                it.id=id;
                if (iver>=2) { std::memcpy(it.type,D+q,4); it.type[4]=0; q+=4; }
                else { std::memcpy(it.type,"v001",4); it.type[4]=0; }
                // name cstring
                size_t ns=q; while (ns< p+isize && D[ns]!=0) ns++;
                it.ctype.assign((const char*)D+q, ns-q); q=ns+1;
                items.push_back(it);
                p += isize;
            }
        } else if (t=="iloc") {
            uint8_t ver=D[b.off+b.hdr]; (void)ver;
            size_t h=b.off+b.hdr;
            int offSz=(D[h+4]>>4)&0xF, lenSz=D[h+4]&0xF, boffSz=(D[h+5]>>4)&0xF;
            size_t p=h+6;
            uint16_t cnt=(uint16_t((D[p]<<8)|D[p+1])); p+=2;
            for (uint16_t i=0;i<cnt;i++) {
                uint16_t id=uint16_t((D[p]<<8)|D[p+1]); p+=2;
                p+=2; // construction method
                p+=2; // data ref index
                p+=boffSz;
                uint16_t ec=uint16_t((D[p]<<8)|D[p+1]); p+=2;
                uint32_t eo=0, el=0;
                for (int e=0;e<(int)ec;e++) {
                    uint32_t o=0,l=0;
                    for(int k=0;k<offSz;k++) o=(o<<8)|D[p++];
                    for(int k=0;k<lenSz;k++) l=(l<<8)|D[p++];
                    eo=o; el=l;
                }
                for (auto& it:items) if (it.id==id) { it.off=eo; it.len=el; }
            }
        } else if (t=="iprp") {
            for (auto& b2 : Walk(D, b.off+b.hdr, b.off+b.size)) {
                if (std::string(b2.type)=="ipco") {
                    for (auto& b3 : Walk(D, b2.off+b2.hdr, b2.off+b2.size)) {
                        Prop pr; pr.kind=b3.type;
                        size_t h3=b3.off+b3.hdr;
                        if (pr.kind=="ispe") {
                            pr.w=(int)(((uint32_t)D[h3+4]<<24)|((uint32_t)D[h3+5]<<16)|((uint32_t)D[h3+6]<<8)|D[h3+7]);
                            pr.h=(int)(((uint32_t)D[h3+8]<<24)|((uint32_t)D[h3+9]<<16)|((uint32_t)D[h3+10]<<8)|D[h3+11]);
                        } else if (pr.kind=="pixi") {
                            pr.nch=D[h3+4];
                            for(int c=0;c<pr.nch && c<8;c++) pr.bits[c]=D[h3+5+c];
                        } else if (pr.kind=="auxC") {
                            size_t s=h3+4; while(s<b3.off+b3.size && D[s]!=0) s++;
                            pr.auxType.assign((const char*)D+h3+4, s-(h3+4));
                        }
                        props.push_back(pr);
                    }
                } else if (std::string(b2.type)=="ipma") {
                    uint8_t ver=D[b2.off+b2.hdr]; (void)ver;
                    size_t p=b2.off+b2.hdr+4;
                    uint32_t cnt=((uint32_t)D[p]<<24)|((uint32_t)D[p+1]<<16)|((uint32_t)D[p+2]<<8)|D[p+3]; p+=4;
                    for (uint32_t i=0;i<cnt;i++) {
                        uint16_t id=uint16_t((D[p]<<8)|D[p+1]); p+=2;
                        uint8_t ac=D[p]; p+=1;
                        std::vector<int> pl;
                        for(int a=0;a<ac;a++){ uint8_t e=D[p]; p++; pl.push_back(e&0x7F); }
                        ipma.push_back({id,pl});
                    }
                }
            }
        }
    }

    auto propsOf=[&](uint16_t id)->std::vector<int> {
        for (auto& m:ipma) if (m.first==id) return m.second;
        return {};
    };

    std::printf("\n===== real HEIC: %s (%lld bytes) =====\n", path.c_str(), (long long)d.size());
    std::printf("primary item id = %u\n", pitm);

    // find exif item and gainmap item
    const ItemInfo* exifIt=nullptr; const ItemInfo* gmIt=nullptr; const ItemInfo* baseIt=nullptr;
    for (auto& it:items) {
        if (std::string(it.type)=="Exif") exifIt=&it;
        auto pl=propsOf(it.id);
        bool isAux=false;
        for (int idx:pl) if (idx>=1 && idx<=(int)props.size() && props[idx-1].kind=="auxC"
                             && props[idx-1].auxType.find("hdrgainmap")!=std::string::npos) isAux=true;
        if (isAux) gmIt=&it;
        if (it.id==pitm) baseIt=&it;
    }

    // base size
    int baseW=0,baseH=0;
    if (baseIt) { auto pl=propsOf(baseIt->id); for(int idx:pl) if(idx>=1&&idx<=(int)props.size()&&props[idx-1].kind=="ispe"){baseW=props[idx-1].w;baseH=props[idx-1].h;} }
    int gmW=0,gmH=0,gmBits=0,gmCh=0; std::string auxType;
    if (gmIt) { auto pl=propsOf(gmIt->id); for(int idx:pl) { if(idx>=1&&idx<=(int)props.size()){
        if(props[idx-1].kind=="ispe"){gmW=props[idx-1].w;gmH=props[idx-1].h;}
        if(props[idx-1].kind=="pixi"){gmCh=props[idx-1].nch;gmBits=props[idx-1].bits[0];}
        if(props[idx-1].kind=="auxC"){auxType=props[idx-1].auxType;}
    }}}
    std::printf("base (item %u)  = %d x %d\n", baseIt?baseIt->id:0, baseW, baseH);
    std::printf("gainmap (item %u) = %d x %d  ch=%d bits=%d\n", gmIt?gmIt->id:0, gmW, gmH, gmCh, gmBits);
    std::printf("auxC = %s\n", auxType.c_str());
    double areaRatio = (double)(gmW*gmH)/(double)(baseW*baseH);
    std::printf("resolution ratio: W=%.4f  H=%.4f  area=%.4f (1/4=%.4f)\n",
                (double)baseW/gmW, (double)baseH/gmH, areaRatio, 0.25);

    // XMP version: scan for HDRGainMapVersion
    unsigned version=0;
    for (auto& it:items) {
        if (it.len==0) continue;
        const uint8_t* s=D+it.off;
        std::string hay((const char*)s, it.len<4096?it.len:4096);
        auto pos=hay.find("HDRGainMapVersion>");
        if (pos!=std::string::npos) {
            size_t vpos=pos+18; std::string num;
            while(vpos<hay.size() && hay[vpos]>='0' && hay[vpos]<='9') num+=hay[vpos++];
            if(!num.empty()) version=(unsigned)std::stoul(num);
        }
    }
    std::printf("XMP HDRGainMapVersion = %u (0x%08x)  %s\n", version, version,
                version==65536?"=v1.0":(version==131072?"=v2.0":(version==0?"(absent)":"(unknown)")));

    // ---- parse MakerNote, derive headroom through the header ----
    bool pass=true;
    if (!exifIt) { std::printf("[FAIL] no Exif item\n"); return 2; }
    float m33=0,m48=0; std::string why;
    if (!ParseMakerNote(D+exifIt->off, exifIt->len, m33, m48, why)) {
        std::printf("[FAIL] MakerNote parse: %s\n", why.c_str()); return 2;
    }
    std::printf("\nMakerNote tag33(HDRHeadroom)=%.6f  tag48(HDRGain)=%.6f\n", m33, m48);

    AppleHeadroomResult r = ComputeHeadroomFromMakerNote(m33, m48, version);
    std::printf("piecewise stops = %.6f\n", r.stops);
    std::printf("headroom = %.6fx  (log2=%.6f)\n", r.headroom, r.headroomLog2);
    std::printf("theoretical max boost = %.6fx  (gain byte 255)\n", gainmap_apple::MaxBoost(r.headroom));

    // ---- assertions vs baked-in vector ----
    using gainmap_apple::AppleImazenVector;
    auto near=[](float a,float b,float e){return std::fabs(a-b)<=e;};
    auto ass=[&](bool c,const char* name,float got,float want){
        std::printf("  [%s] %s  got=%.6f want=%.6f\n", c?"PASS":"FAIL", name, got, want);
        if(!c) pass=false;
    };
    ass(r.ok, "headroom.ok", r.ok?1.f:0.f, 1.f);
    ass(near(r.headroom, AppleImazenVector::kHeadroom, 5e-2f), "headroom.value", r.headroom, AppleImazenVector::kHeadroom);
    ass(near(r.stops, AppleImazenVector::kStops, 2e-3f), "stops.value", r.stops, AppleImazenVector::kStops);
    ass(near((float)areaRatio, 0.25f, 0.01f), "areaRatio.quarter", (float)areaRatio, 0.25f);
    ass(gmCh==1 && gmBits==8, "gainmap.8bitSingleChannel", (float)(gmCh*10+gmBits), 18.f);
    ass(version==65536u, "version.v10", (float)version, 65536.f);

    bool allOk = stOk && pass;
    std::printf("\n===== OVERALL: %s =====\n", allOk ? "PASS" : "FAIL");
    return allOk ? 0 : 1;
}

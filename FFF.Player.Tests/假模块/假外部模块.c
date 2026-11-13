/*
 * 测试用最小外部模块。只验证宿主侧装载/描述符/能力表/所有权行为，不实现任何
 * 色彩或音频算法。分支由环境变量 FFF_TEST_MODULE_BRANCH 选择：
 *   1 = legacy v3 8 B 空信封
 *   2 = legacy 32 B v2 表
 *   3 = legacy 8 B v2 恶意/坏版本信封
 *   4 = legacy 只有 AudioPassthrough
 *   5 = legacy 只有 AudioRender
 *   6 = legacy 缺所有可选导出（仅色彩表）
 *   7 = 中性 descriptor + 只有 Processor
 *   8 = 中性 descriptor + Processor + AudioRender
 *   9 = 中性 descriptor + Processor + Authorization（锁定；good 返回“已接受未落盘”）
 */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>

#define BRANCH_ENV "FFF_TEST_MODULE_BRANCH"

typedef struct {
    uint32_t size;
    uint32_t version;
    void* context;
    void* name;
    void* support;
    void* render;
    void* reset;
    void* release;
} AudioRenderTable;

typedef struct {
    uint32_t size;
    uint32_t version;
    void* context;
    void* describe;
    void* pack;
    void* reset;
    void* release;
} AudioPassthroughTable;

typedef struct {
    uint32_t size;
    uint32_t version;
    void* start;
    void* stop;
    void* read;
} InjectionTable;

typedef struct {
    uint32_t size;
    uint32_t version;
    void* context;
    void* getState;
    void* getText;
    void* submit;
} AuthApi;

typedef struct {
    uint32_t size;
    uint32_t version;
    void* context;
    void* start;
    void* stop;
    void* getStatus;
} ProcessorApi;

typedef struct {
    uint32_t size;
    uint32_t version;
    void* context;
    void* getDescriptor;
    void* getCapability;
} ModuleApi;

typedef struct {
    uint32_t size;
    uint32_t version;
    void* id;
    void* displayName;
    uint32_t capabilities;
    uint32_t requirements;
    uint32_t flags;
} Descriptor;

typedef struct {
    uint32_t size;
    uint32_t version;
    uint32_t running;
    uint32_t padding;
    uint64_t processedFrames;
    uint64_t unchangedFrames;
    uint64_t skippedFrames;
    uint64_t unattributedFrames;
    int64_t lastFrameIndex;
    float lastPeakNits;
    char statusText[64];
    char detailText[64];
} ProcessorStatus;

static int cached_branch = -1;

static int read_branch(void)
{
    char text[16] = {0};
    DWORD n = GetEnvironmentVariableA(BRANCH_ENV, text, (DWORD)(sizeof(text) - 1));
    if (n == 0) return 1;
    return atoi(text);
}

static int branch(void)
{
    if (cached_branch < 0) cached_branch = read_branch();
    return cached_branch;
}

static int dummy_bool(void* context) { (void)context; return 1; }
static int dummy_render(void* context, void* frame) { (void)context; (void)frame; return 0; }
static void dummy_reset(void* context) { (void)context; }
static void dummy_release(void* context) { (void)context; }
static void dummy_start(void* context) { (void)context; }
static void dummy_stop(void* context) { (void)context; }

static uint32_t running_state = 1;

static int processor_start(void* context, void* session)
{
    (void)context; (void)session;
    running_state = 1;
    return 0;
}

static void processor_stop(void* context)
{
    (void)context;
    running_state = 0;
}

static void processor_status(void* context, ProcessorStatus* status)
{
    (void)context;
    if (!status) return;
    status->size = (uint32_t)sizeof(ProcessorStatus);
    status->version = 1;
    status->running = running_state;
    status->processedFrames = 9;
    status->unchangedFrames = 3;
    status->skippedFrames = 2;
    status->unattributedFrames = 1;
    status->lastFrameIndex = 21;
    status->lastPeakNits = 321.5f;
    lstrcpynA(status->statusText, "处理中", 64);
    lstrcpynA(status->detailText, "假模块详情", 64);
}

static int auth_ready = 0;
static int auth_state(void* context)
{
    (void)context;
    if (branch() == 9) return auth_ready ? 2 : 1;
    return 2;
}
static const char* auth_text(void* context, uint32_t purpose)
{
    (void)context; (void)purpose;
    return "假模块授权文本";
}
static int auth_submit(void* context, const char* code)
{
    (void)context;
    if (!code || lstrcmpA(code, "good") != 0) return 0;
    if (branch() == 9) {
        auth_ready = 1;
        return 2;  /* accepted, but deliberately not persisted */
    }
    return 1;
}

static ModuleApi module_api;
static AuthApi auth_api;
static ProcessorApi processor_api;
static AudioRenderTable render_table;
static AudioPassthroughTable passthrough_table;
static InjectionTable injection_table;

static void fill_render(AudioRenderTable* table)
{
    if (!table) return;
    table->size = (uint32_t)sizeof(AudioRenderTable);
    table->version = 1;
    table->context = (void*)0x41;
    table->name = (void*)dummy_bool;
    table->support = (void*)dummy_bool;
    table->render = (void*)dummy_render;
    table->reset = (void*)dummy_reset;
    table->release = (void*)dummy_release;
}

static void fill_passthrough(AudioPassthroughTable* table)
{
    if (!table) return;
    table->size = (uint32_t)sizeof(AudioPassthroughTable);
    table->version = 1;
    table->context = (void*)0x42;
    table->describe = (void*)dummy_bool;
    table->pack = (void*)dummy_bool;
    table->reset = (void*)dummy_reset;
    table->release = (void*)dummy_release;
}

static int module_api_ready(void);

__declspec(dllexport) int __cdecl FFF3FP_ExternalModuleGetApi(uint32_t abi, ModuleApi* out_api)
{
    cached_branch = read_branch();
    int b = cached_branch;
    if (b != 7 && b != 8 && b != 9) return 1;
    if (!out_api) return 1;
    if (abi != 1) return 1;
    module_api_ready();
    out_api->size = (uint32_t)sizeof(ModuleApi);
    out_api->version = 1;
    out_api->context = (void*)0x50;
    out_api->getDescriptor = module_api.getDescriptor;
    out_api->getCapability = module_api.getCapability;
    return 0;
}

static void fill_descriptor(Descriptor* descriptor)
{
    static const char* id = "test.fake.external";
    static const char* name = "假外部模块";
    int b = branch();
    if (!descriptor) return;
    descriptor->size = (uint32_t)sizeof(Descriptor);
    descriptor->version = 1;
    descriptor->id = (void*)id;
    descriptor->displayName = (void*)name;
    descriptor->capabilities = (b == 8) ? (8u | 2u) : ((b == 9) ? (8u | 1u) : 8u);
    descriptor->requirements = 0;
    descriptor->flags = 0;
}

static int get_descriptor(void* context, Descriptor* descriptor)
{
    (void)context;
    fill_descriptor(descriptor);
    return 0;
}

static int get_capability(void* context, uint32_t capability, uint32_t version, void** table)
{
    (void)context; (void)version;
    if (!table) return 1;
    if (capability == 1 && branch() == 9) {
        auth_api.size = (uint32_t)sizeof(AuthApi);
        auth_api.version = 1;
        auth_api.context = (void*)0x52;
        auth_api.getState = (void*)auth_state;
        auth_api.getText = (void*)auth_text;
        auth_api.submit = (void*)auth_submit;
        *table = &auth_api;
        return 0;
    }
    if (capability == 4) {
        processor_api.size = (uint32_t)sizeof(ProcessorApi);
        processor_api.version = 1;
        processor_api.context = (void*)0x51;
        processor_api.start = (void*)processor_start;
        processor_api.stop = (void*)processor_stop;
        processor_api.getStatus = (void*)processor_status;
        *table = &processor_api;
        return 0;
    }
    if (capability == 2 && branch() == 8) {
        fill_render(&render_table);
        *table = &render_table;
        return 0;
    }
    *table = 0;
    return 1;
}

/* Legacy exports. */
__declspec(dllexport) int __cdecl FFF_GetColorTransformApi(uint32_t version, void* table)
{
    (void)version;
    cached_branch = read_branch();
    int b = cached_branch;
    uint32_t* words = (uint32_t*)table;
    if (!words) return 1;
    if (b == 2) {
        words[0] = 32; words[1] = 2;
        return 0;
    }
    if (b == 3) {
        words[0] = 8; words[1] = 2;
        return 0;
    }
    words[0] = 8; words[1] = 3;
    return 0;
}

__declspec(dllexport) uint32_t __cdecl FFF_ColorTransformAuthorizationStatus(void)
{
    return 2;
}

__declspec(dllexport) const char* __cdecl FFF_ColorTransformStatusText(uint32_t purpose)
{
    (void)purpose;
    return "假模块状态文本";
}

__declspec(dllexport) int __cdecl FFF_ColorTransformAuthenticate(const char* code)
{
    return (code && lstrcmpA(code, "good") == 0) ? 1 : 0;
}

__declspec(dllexport) int __cdecl FFF_GetAudioRenderApi(uint32_t version, void* table)
{
    (void)version;
    if (branch() == 4 || branch() == 6) return 1;
    fill_render((AudioRenderTable*)table);
    return 0;
}

__declspec(dllexport) int __cdecl FFF_GetAudioPassthroughApi(uint32_t version, void* table)
{
    (void)version;
    if (branch() == 5 || branch() == 6) return 1;
    fill_passthrough((AudioPassthroughTable*)table);
    return 0;
}

__declspec(dllexport) int __cdecl FFF_GetInjectionApi(uint32_t version, void* table)
{
    (void)version;
    if (branch() == 6) return 1;
    InjectionTable* out = (InjectionTable*)table;
    if (!out) return 1;
    out->size = (uint32_t)sizeof(InjectionTable);
    out->version = 1;
    out->start = (void*)dummy_start;
    out->stop = (void*)dummy_stop;
    out->read = (void*)dummy_bool;
    return 0;
}

/* 新入口的 module api 需要函数指针；上面的静态初始化不能取函数地址，这里补。 */
static int module_api_ready(void)
{
    module_api.getDescriptor = (void*)get_descriptor;
    module_api.getCapability = (void*)get_capability;
    return 0;
}

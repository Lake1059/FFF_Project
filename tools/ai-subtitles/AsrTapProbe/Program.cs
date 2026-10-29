// AsrTapProbe: P2a gate harness for the ASR audio tap (FFF.Player.AudioTap.h).
//
// Verifies, against the real playback path:
//   1. the four tap exports resolve and behave (enable / read / stats / disable);
//   2. dumped blocks carry a sane bed: channels, sample rate, monotonic media
//      positions, and the generation stamped by open/seek;
//   3. seek empties the ring (the next read reports empty, then new-generation
//      blocks arrive);
//   4. A/B: decoding keeps the same pace with the tap on as with it off, i.e.
//      the hot-path cost while off is one pointer load.
//
// Run: AsrTapProbe.exe <media> [secondsPerPhase]
// Needs FFF.Native.dll next to the exe and ffmpeg-full\ beside it (same layout
// as PerfTest); the probe sets the DLL directory itself.

using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

enum FFF3FPDecodeMode : uint { Unspecified = 0, Cpu = 1, Gpu = 2, D3D11 = Gpu }
enum FFF3FPColorMode : uint { MapToSdr = 0, RawHdrAsSdr = 1, MapToHdr = 2 }
enum FFF3FPState : uint { Idle = 0, Opening = 1, Ready = 2, Playing = 3, Paused = 4, Ended = 5, Failed = 6, Closed = 7 }
enum FFF3FPEvent : uint { StateChanged = 1, OpenCompleted = 2, OperationCompleted = 3, PlaybackEnded = 4, Error = 5, ColorModeChanged = 6, DeviceChanged = 7 }

[UnmanagedFunctionPointer(System.Runtime.InteropServices.CallingConvention.Cdecl)]
delegate void FFF3FPEventCallback(IntPtr context, FFF3FPEvent eventType, [MarshalAs(UnmanagedType.LPStr)] string detailJsonUtf8);

// ---- FFF.Player.AudioTap.h mirror (must stay field-for-field identical) ----
[StructLayout(LayoutKind.Sequential)]
struct FFF3FPAudioTapConfig {
    public uint size;
    public uint version;
    public uint ringCapacityBytes;
    public uint flags;
}

[StructLayout(LayoutKind.Sequential)]
struct FFF3FPAudioTapBlock {
    public uint size;
    public uint version;
    public uint bytesWritten;
    public uint requiredBytes;
    public ulong generation;
    public long position100ns;
    public uint frames;
    public uint sampleRate;
    public uint channels;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 24)] public int[] positions;
    public int sampleFormat;
}

[StructLayout(LayoutKind.Sequential)]
struct FFF3FPAudioTapStats {
    public uint size;
    public uint version;
    public ulong blocksProduced;
    public ulong blocksDropped;
    public ulong bufferedBytes;
}

[StructLayout(LayoutKind.Sequential)]
struct FFF3FPConfiguration {
    public uint size;
    public uint version;
    public IntPtr outputWindow;
    public FFF3FPDecodeMode decodeMode;
    public FFF3FPColorMode colorMode;
    public float sdrPeakNits;
    public float hdrPeakNits;
    public float sdrPaperWhiteNits;
    [MarshalAs(UnmanagedType.LPStr)] public string audioEndpointIdUtf8;
    public FFF3FPEventCallback eventCallback;
    public IntPtr eventCallbackContext;
    public uint videoScalingQuality;
    public uint forceHdrOutput;
    public int preferredAdapterIndex;
    public uint sdrScRgbMode;
    public uint adaptiveDownscaleBeforeUpload;
    public uint adaptiveDownscaleDropPercent;
    public uint adaptiveDecoderThreads;
    public uint minSoftwareDecoderThreads;
    public uint maxSoftwareDecoderThreads;
    public uint decoderGrowDropPercent;
    public uint decoderShrinkDropPercent;
}

[StructLayout(LayoutKind.Sequential)]
struct FFF3FPSnapshot {
    public uint size;
    public uint version;
    public FFF3FPState state;
    public FFF3FPDecodeMode decodeMode;
    public FFF3FPColorMode requestedColorMode;
    public FFF3FPColorMode actualColorMode;
    public long position100ns;
    public long duration100ns;
    public long frameIndex;
    public long framePts;
    public int frameTimeBaseNumerator;
    public int frameTimeBaseDenominator;
    public int selectedVideoStream;
    public int selectedAudioStream;
    public uint videoWidth;
    public uint videoHeight;
    public uint isHdrSource;
    public uint isExternalAudio;
    public long externalAudioOffset100ns;
    public ulong decodedVideoFrames;
    public ulong presentedVideoFrames;
    public ulong droppedVideoFrames;
    public uint queuedVideoFrames;
    public uint sourcePeakNits;
    public ulong decodedAudioFrames;
    public long audioPosition100ns;
    public long bufferedAudio100ns;
    public ulong audioUnderruns;
    public ulong audioTimestampJitterFrames;
    public ulong audioDiscontinuities;
    public ulong audioInsertedSilenceFrames;
    public ulong audioDroppedOverlapFrames;
    public ulong coalescedVideoFrames;
    public ulong audioRejectedFrames;
    public ulong swapChainPresents;
    public ulong presentWait100ns;
    public ulong deviceLockWait100ns;
    public ulong hardwareTransfer100ns;
    public ulong softwareConvert100ns;
    public ulong videoBitRate;
    public ulong audioBitRate;
    public uint videoOutputBitDepth;
    public uint videoScalingMode;
    public ulong timelineGeneration;
    public uint hdrFormat;
    public uint compatibleHdrFormats;
    public uint hdrProcessingPath;
    public uint hdrDpProfile;
    public uint hdrDpLevel;
    public uint hasHDRdpRpu;
    public uint hasHDRdpEnhancementLayer;
    public uint hdrDpEnhancementLayer;
    public uint dynamicHdrMetadataActive;
    public uint hdrFallbackActive;
    public uint displayMinLuminanceMilliNits;
    public uint displayPeakNits;
    public uint displayFullFramePeakNits;
    public uint effectiveTargetPeakNits;
    public uint dynamicMetadataWindows;
    public uint dynamicMetadataDegrade;
    public ulong dynamicMetadataSerial;
    public ulong dynamicMetadataHeldFrames;
    public uint dynamicMetadataTargetedNits;
    public uint iamfActive;
    public uint iamfChannels;
    public int iamfSoundSystem;
    public ulong videoUpload100ns;
    public uint iamfContentChannels;
    public uint iamfBinaural;
    public uint audioBinaural;
    public long audioDelay100ns;
    public ulong uploadMapWait100ns;
    public ulong uploadCopy100ns;
    public ulong downscaleResample100ns;
    public ulong scalePassCount;
    public ulong uploadCount;
    public ulong uploadCopyBytes;
    public uint uploadPath;
}

enum FFFResult : int { Success = 0, InvalidArgument = -1, InvalidState = -2, BufferTooSmall = -3 }

static class Native {
    public const string DllName = "FFF.Native.dll";
    public const uint FFF3FP_AUDIO_TAP_VERSION = 1u;
    public const uint FFF3FP_AUDIO_TAP_DEFAULT_RING_BYTES = 32u * 1024u * 1024u;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool SetDllDirectory(string lpPathName);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint FFF3FP_GetApiVersion();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_Create(ref FFF3FPConfiguration configuration, out IntPtr player);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_Open(IntPtr player, [MarshalAs(UnmanagedType.LPStr)] string localPathUtf8);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_Play(IntPtr player);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_Pause(IntPtr player);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_Stop(IntPtr player);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_Close(IntPtr player);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_Seek(IntPtr player, long position100ns);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_GetSnapshot(IntPtr player, ref FFF3FPSnapshot snapshot);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void FFF3FP_Destroy(IntPtr player);

    // ---- ASR audio tap (FFF.Player.AudioTap.h) ----
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_EnableAudioTap(IntPtr player, ref FFF3FPAudioTapConfig config);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_DisableAudioTap(IntPtr player);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_ReadAudioTap(IntPtr player, byte[] dst, uint capacity, ref FFF3FPAudioTapBlock block);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_GetAudioTapStats(IntPtr player, ref FFF3FPAudioTapStats stats);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern FFFResult FFF3FP_GetAudioPeakLevels(IntPtr player, ref FFF3FPAudioPeakLevels levels);
}

[StructLayout(LayoutKind.Sequential)]
struct FFF3FPAudioPeakLevels {
    public uint size;
    public uint version;
    public uint channelCount;
    public uint inputChannelCount;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] public float[] values;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] public float[] inputValues;
}

static class Win32 {
    public delegate IntPtr WNDPROC(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct WNDCLASSEX {
        public uint cbSize, style; public IntPtr lpfnWndProc; public int cbClsExtra, cbWndExtra;
        public IntPtr hInstance, hIcon, hCursor, hbrBackground; public string lpszMenuName, lpszClassName; public IntPtr hIconSm;
    }
    public const uint WS_OVERLAPPEDWINDOW = 0x00CF0000;
    public const int CW_USEDEFAULT = unchecked((int)0x80000000);
    public const int SM_CXSCREEN = 0, SM_CYSCREEN = 1;
    public const uint WM_DESTROY = 0x0002;

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern ushort RegisterClassEx(ref WNDCLASSEX wc);
    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateWindowEx(uint exStyle, string className, string windowName, uint style,
        int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);
    [DllImport("user32.dll")]
    public static extern bool AdjustWindowRectEx(ref RECT rect, uint style, bool menu, uint exStyle);
    [DllImport("user32.dll")]
    public static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")]
    public static extern bool DestroyWindow(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr DefWindowProc(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int show);
    [DllImport("user32.dll")]
    public static extern bool UpdateWindow(IntPtr hWnd);
    [DllImport("kernel32.dll")]
    public static extern IntPtr GetModuleHandle(string name);
    [DllImport("user32.dll")]
    public static extern bool PostQuitMessage(int code);

    [StructLayout(LayoutKind.Sequential)]
    public struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam; public uint time; public int ptX, ptY; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetMessage(out MSG msg, IntPtr hWnd, uint min, uint max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool TranslateMessage(ref MSG msg);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr DispatchMessage(ref MSG msg);
}

static class Program {
    static string lastEvent = "";
    static readonly FFF3FPEventCallback eventCb = (ctx, type, json) => { lastEvent = $"{type}: {json}"; };
    static Win32.WNDPROC defProc = (hWnd, uMsg, wParam, lParam) => {
        if (uMsg == Win32.WM_DESTROY) { Win32.PostQuitMessage(0); return IntPtr.Zero; }
        return Win32.DefWindowProc(hWnd, uMsg, wParam, lParam);
    };

    static IntPtr CreateDummyWindow(int w, int h) {
        var wc = new Win32.WNDCLASSEX {
            cbSize = (uint)Marshal.SizeOf<Win32.WNDCLASSEX>(),
            style = 0,
            lpfnWndProc = Marshal.GetFunctionPointerForDelegate<Win32.WNDPROC>(defProc),
            cbClsExtra = 0, cbWndExtra = 0,
            hInstance = Win32.GetModuleHandle(null),
            hIcon = IntPtr.Zero, hCursor = IntPtr.Zero, hbrBackground = IntPtr.Zero,
            lpszMenuName = null, lpszClassName = "AsrTapProbeWindowClass", hIconSm = IntPtr.Zero
        };
        Win32.RegisterClassEx(ref wc);
        var rect = new Win32.RECT { left = 0, top = 0, right = w, bottom = h };
        Win32.AdjustWindowRectEx(ref rect, Win32.WS_OVERLAPPEDWINDOW, false, 0);
        var hwnd = Win32.CreateWindowEx(0, "AsrTapProbeWindowClass", "AsrTapProbe",
            Win32.WS_OVERLAPPEDWINDOW, Win32.CW_USEDEFAULT, Win32.CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            IntPtr.Zero, IntPtr.Zero, Win32.GetModuleHandle(null), IntPtr.Zero);
        Win32.ShowWindow(hwnd, 1);
        Win32.UpdateWindow(hwnd);
        return hwnd;
    }

    static string Name(FFFResult r) => r.ToString();

    // The DXGI present path needs a window message pump; without one the video
    // renderer stalls and the decode pipeline backs up (PerfTest runs the same
    // pump). Audio-only consumers would not need it, the real player always has
    // one, and the probe plays real video files.
    static void StartMessagePump() {
        var pump = new Thread(() => {
            Win32.MSG msg;
            while (Win32.GetMessage(out msg, IntPtr.Zero, 0, 0) > 0) {
                Win32.TranslateMessage(ref msg);
                Win32.DispatchMessage(ref msg);
            }
        }) { IsBackground = true };
        pump.Start();
    }

    // Opens `media` fresh and plays it; returns false if it never reached Playing.
    static bool OpenAndPlay(IntPtr player, string media, int waitMs = 10000) {
        if (Native.FFF3FP_Close(player) != FFFResult.Success) { /* not open yet */ }
        var open = Native.FFF3FP_Open(player, media);
        if (open != FFFResult.Success) { Console.WriteLine($"FAIL: Open = {Name(open)}"); return false; }
        var snap = new FFF3FPSnapshot { size = (uint)Marshal.SizeOf<FFF3FPSnapshot>(), version = Native.FFF3FP_GetApiVersion() };
        for (int i = 0; i < waitMs / 50; i++) {
            if (Native.FFF3FP_GetSnapshot(player, ref snap) == FFFResult.Success && snap.state >= FFF3FPState.Ready) break;
            Thread.Sleep(50);
        }
        if (snap.state < FFF3FPState.Ready) { Console.WriteLine($"FAIL: never reached Ready (state={snap.state})"); return false; }
        if (Native.FFF3FP_Play(player) != FFFResult.Success) { Console.WriteLine("FAIL: Play"); return false; }
        for (int i = 0; i < 100; i++) {
            Native.FFF3FP_GetSnapshot(player, ref snap);
            if (snap.state == FFF3FPState.Playing) return true;
            Thread.Sleep(50);
        }
        Console.WriteLine($"FAIL: never reached Playing (state={snap.state})");
        return false;
    }

    // Pumps the tap into `writer`; returns blocks drained and the total PCM bytes.
    static (long blocks, long bytes) DrainTap(IntPtr player, BinaryWriter writer, double seconds) {
        var buffer = new byte[1 << 20];
        long blocks = 0, bytes = 0;
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(seconds);
        while (DateTime.UtcNow < deadline) {
            var block = new FFF3FPAudioTapBlock { positions = new int[24] };
            var r = Native.FFF3FP_ReadAudioTap(player, buffer, (uint)buffer.Length, ref block);
            if (r == FFFResult.BufferTooSmall) {
                Console.WriteLine($"  [tap] BufferTooSmall: need {block.requiredBytes} bytes");
                buffer = new byte[Math.Max(block.requiredBytes, 1 << 20)];
                continue;
            }
            if (r != FFFResult.Success) {
                Console.WriteLine($"  [tap] read failed: {Name(r)}");
                break;
            }
            if (block.bytesWritten == 0) { Thread.Sleep(2); continue; }
            writer.Write(block.generation);
            writer.Write(block.position100ns);
            writer.Write(block.frames);
            writer.Write(block.sampleRate);
            writer.Write(block.channels);
            writer.Write(block.sampleFormat);
            writer.Write(block.bytesWritten);
            for (int i = 0; i < 24; i++) writer.Write(block.positions[i]);
            writer.Write(buffer, 0, (int)block.bytesWritten);
            blocks++;
            bytes += block.bytesWritten;
        }
        return (blocks, bytes);
    }

    static void Main(string[] args) {
        if (args.Length < 1) {
            Console.WriteLine("Usage: AsrTapProbe.exe <media> [secondsPerPhase]");
            return;
        }
        var media = args[0];
        var phaseSeconds = 8.0;
        if (args.Length > 1 && !double.TryParse(args[1], out phaseSeconds)) phaseSeconds = 8.0;

        var ffmpegDir = Path.Combine(AppContext.BaseDirectory, "ffmpeg-full");
        if (Directory.Exists(ffmpegDir)) Native.SetDllDirectory(ffmpegDir);
        Console.WriteLine($"api version = {Native.FFF3FP_GetApiVersion()}");

        var hwnd = CreateDummyWindow(640, 480);
        StartMessagePump();
        var cfg = new FFF3FPConfiguration {
            size = (uint)Marshal.SizeOf<FFF3FPConfiguration>(),
            version = Native.FFF3FP_GetApiVersion(),
            outputWindow = hwnd,
            decodeMode = FFF3FPDecodeMode.Gpu,
            colorMode = FFF3FPColorMode.MapToSdr,
            sdrPeakNits = 203f, hdrPeakNits = 1000f, sdrPaperWhiteNits = 203f,
            audioEndpointIdUtf8 = null,
            eventCallback = eventCb, eventCallbackContext = IntPtr.Zero,
            videoScalingQuality = 0, forceHdrOutput = 0, preferredAdapterIndex = -1, sdrScRgbMode = 0,
            adaptiveDownscaleBeforeUpload = 0, adaptiveDownscaleDropPercent = 20,
            adaptiveDecoderThreads = 0, minSoftwareDecoderThreads = 4, maxSoftwareDecoderThreads = 32,
            decoderGrowDropPercent = 10, decoderShrinkDropPercent = 2,
        };
        if (Native.FFF3FP_Create(ref cfg, out var player) != FFFResult.Success) {
            Console.WriteLine("FAIL: FFF3FP_Create");
            return;
        }
        var open = Native.FFF3FP_Open(player, media);
        Console.WriteLine($"open({media}) = {Name(open)}  (snapshot size = {Marshal.SizeOf<FFF3FPSnapshot>()})");
        if (open != FFFResult.Success) { Native.FFF3FP_Destroy(player); return; }
        var snap = new FFF3FPSnapshot { size = (uint)Marshal.SizeOf<FFF3FPSnapshot>(), version = Native.FFF3FP_GetApiVersion() };
        for (int i = 0; i < 100; i++) {
            Thread.Sleep(100);
            if (Native.FFF3FP_GetSnapshot(player, ref snap) != FFFResult.Success) {
                Console.WriteLine($"FAIL: GetSnapshot rejected (size={snap.size}, version={snap.version})");
                Native.FFF3FP_Destroy(player);
                return;
            }
            if (snap.state >= FFF3FPState.Ready) break;
        }
        Console.WriteLine($"state = {snap.state}, duration = {snap.duration100ns / 10000000.0:F1}s, " +
            $"audio stream = {snap.selectedAudioStream}, generation = {snap.timelineGeneration}");

        // ---- Phase A: tap off (baseline pace), fresh open so the file cannot
        // have ended before the measurement window closes.
        if (!OpenAndPlay(player, media)) { Native.FFF3FP_Destroy(player); return; }
        Thread.Sleep(300);
        Native.FFF3FP_GetSnapshot(player, ref snap);
        var a0 = snap.decodedAudioFrames;
        var t0 = DateTime.UtcNow;
        Thread.Sleep((int)(phaseSeconds * 1000));
        Native.FFF3FP_GetSnapshot(player, ref snap);
        var a1 = snap.decodedAudioFrames;
        var paceOff = (a1 - a0) / Math.Max(0.001, (DateTime.UtcNow - t0).TotalSeconds);
        Console.WriteLine($"[A tap-off] decoded audio frames = {a1 - a0} in {phaseSeconds:F1}s " +
            $"({paceOff:F0} frames/s), state = {snap.state}, position = {snap.position100ns / 1e7:F2}s");

        // ---- Phase B/C/D: tap on, one open for drain + seek checks ----
        var tapCfg = new FFF3FPAudioTapConfig {
            size = (uint)Marshal.SizeOf<FFF3FPAudioTapConfig>(),
            version = Native.FFF3FP_AUDIO_TAP_VERSION,
            ringCapacityBytes = Native.FFF3FP_AUDIO_TAP_DEFAULT_RING_BYTES,
            flags = 0,
        };
        var en = Native.FFF3FP_EnableAudioTap(player, ref tapCfg);
        Console.WriteLine($"EnableAudioTap = {Name(en)}");
        if (en != FFFResult.Success) { Native.FFF3FP_Stop(player); Native.FFF3FP_Destroy(player); return; }
        if (!OpenAndPlay(player, media)) { Native.FFF3FP_Destroy(player); return; }

        // The dump defaults next to the exe but can be redirected (the schtasks
        // dispatch runs elevated, and files it writes under the workspace pick
        // up an owner our shells cannot read back).
        var dumpPath = Environment.GetEnvironmentVariable("ASR_TAP_DUMP")
            ?? Path.Combine(AppContext.BaseDirectory, "asr_tap_dump.bin");
        long blocks = 0, bytes = 0;
        try {
            using (var writer = new BinaryWriter(File.Open(dumpPath, FileMode.Create))) {
            Thread.Sleep(300);
            Native.FFF3FP_GetSnapshot(player, ref snap);
            var b0 = snap.decodedAudioFrames;
            var bt0 = DateTime.UtcNow;
            (blocks, bytes) = DrainTap(player, writer, phaseSeconds);
            Native.FFF3FP_GetSnapshot(player, ref snap);
            var b1 = snap.decodedAudioFrames;
            var paceOn = (b1 - b0) / Math.Max(0.001, (DateTime.UtcNow - bt0).TotalSeconds);
            Console.WriteLine($"[B tap-on ] decoded audio frames = {b1 - b0} in {phaseSeconds:F1}s " +
                $"({paceOn:F0} frames/s), ratio vs off = {paceOn / Math.Max(1, paceOff):F3}");
            Console.WriteLine($"[B tap-on ] drained {blocks} blocks / {bytes} PCM bytes -> {dumpPath}");

            var stats = new FFF3FPAudioTapStats { size = (uint)Marshal.SizeOf<FFF3FPAudioTapStats>() };
            var st = Native.FFF3FP_GetAudioTapStats(player, ref stats);
            Console.WriteLine($"[B stats  ] GetAudioTapStats = {Name(st)}: produced={stats.blocksProduced} " +
                $"dropped={stats.blocksDropped} buffered={stats.bufferedBytes}");

            // Cross-check: the renderer's own peak levels prove whether the
            // decoded audio has content at all (a silent source track would make
            // an all-zero tap dump expected rather than a tap bug).
            var peaks = new FFF3FPAudioPeakLevels {
                size = (uint)Marshal.SizeOf<FFF3FPAudioPeakLevels>(),
                version = Native.FFF3FP_GetApiVersion(),
                values = new float[8], inputValues = new float[8],
            };
            var pk = Native.FFF3FP_GetAudioPeakLevels(player, ref peaks);
            var inV = string.Join(",", Array.ConvertAll(peaks.inputValues, v => v.ToString("F4")));
            var outV = string.Join(",", Array.ConvertAll(peaks.values, v => v.ToString("F4")));
            Console.WriteLine($"[B peaks  ] GetAudioPeakLevels = {Name(pk)}: in={peaks.inputChannelCount}ch [{inV}] " +
                $"out={peaks.channelCount}ch [{outV}]");

            // ---- Phase C: seek empties the ring ----
            var target = Math.Max(0, snap.duration100ns / 3);
            var seek = Native.FFF3FP_Seek(player, target);
            Console.WriteLine($"[C seek   ] Seek(+{target / 1e7:F1}s) = {Name(seek)}");
            // A seek from Ended parks the session in Paused; resume so the
            // post-seek blocks the gate asks about actually arrive.
            var resume = Native.FFF3FP_Play(player);
            Console.WriteLine($"[C seek   ] Play after seek = {Name(resume)}");
            var genBefore = snap.timelineGeneration;
            var emptyStreak = 0;
            var cDeadline = DateTime.UtcNow + TimeSpan.FromSeconds(2);
            while (DateTime.UtcNow < cDeadline) {
                var probe = new FFF3FPAudioTapBlock { positions = new int[24] };
                var rr = Native.FFF3FP_ReadAudioTap(player, new byte[1 << 20], 1 << 20, ref probe);
                if (rr == FFFResult.Success && probe.bytesWritten == 0) { emptyStreak++; Thread.Sleep(5); }
                else { emptyStreak = 0; if (probe.generation > genBefore) break; }
                if (emptyStreak >= 3) break;
            }
            Console.WriteLine($"[C seek   ] ring empty streak = {emptyStreak} (>=3 expected), generation " +
                $"{genBefore} -> (see next block)");

            // ---- Phase D: post-seek blocks carry the new generation ----
            long postBlocks = 0; ulong postGen = 0; long lastPos = long.MinValue; var posOk = true;
            var dDeadline = DateTime.UtcNow + TimeSpan.FromSeconds(3);
            while (postBlocks < 5 && DateTime.UtcNow < dDeadline) {
                var probe = new FFF3FPAudioTapBlock { positions = new int[24] };
                var rr = Native.FFF3FP_ReadAudioTap(player, new byte[1 << 20], 1 << 20, ref probe);
                if (rr == FFFResult.Success && probe.bytesWritten > 0) {
                    if (postBlocks == 0) postGen = probe.generation;
                    if (probe.position100ns != long.MinValue) {
                        if (lastPos != long.MinValue && probe.position100ns <= lastPos) posOk = false;
                        lastPos = probe.position100ns;
                    }
                    postBlocks++;
                } else Thread.Sleep(5);
            }
            Console.WriteLine($"[D post   ] post-seek blocks = {postBlocks}, generation = {postGen} " +
                $"(pre-seek {genBefore}), positions monotonic = {posOk}");
            }
        } catch (Exception ex) {
            Console.WriteLine($"[DUMP-FAIL] {ex.GetType().Name} hr=0x{ex.HResult:X8} msg={ex.Message}");
            // Diagnose: can we write next to the exe at all from this process?
            try {
                var alt = Path.Combine(AppContext.BaseDirectory, "diag_write_test.bin");
                File.WriteAllBytes(alt, new byte[] { 1, 2, 3 });
                File.Delete(alt);
                Console.WriteLine("[DUMP-DIAG] write next to exe = OK");
            } catch (Exception ex2) {
                Console.WriteLine($"[DUMP-DIAG] write next to exe FAILED: {ex2.GetType().Name} hr=0x{ex2.HResult:X8}");
            }
            throw;
        }

        var stats2 = new FFF3FPAudioTapStats { size = (uint)Marshal.SizeOf<FFF3FPAudioTapStats>() };
        Native.FFF3FP_GetAudioTapStats(player, ref stats2);
        Console.WriteLine($"[D stats  ] produced={stats2.blocksProduced} dropped={stats2.blocksDropped} " +
            $"buffered={stats2.bufferedBytes}");

        Native.FFF3FP_Stop(player);
        var dis = Native.FFF3FP_DisableAudioTap(player);
        Console.WriteLine($"DisableAudioTap = {Name(dis)}");
        var stats3 = new FFF3FPAudioTapStats { size = (uint)Marshal.SizeOf<FFF3FPAudioTapStats>() };
        var st3 = Native.FFF3FP_GetAudioTapStats(player, ref stats3);
        Console.WriteLine($"[E stats  ] after disable: produced={stats3.blocksProduced} " +
            $"dropped={stats3.blocksDropped} buffered={stats3.bufferedBytes} (all zero expected)");
        var readAfter = new FFF3FPAudioTapBlock { positions = new int[24] };
        var ra = Native.FFF3FP_ReadAudioTap(player, new byte[64], 64, ref readAfter);
        Console.WriteLine($"[E read   ] after disable = {Name(ra)}, bytesWritten={readAfter.bytesWritten} (Success/0 expected)");

        Native.FFF3FP_Destroy(player);
        if (hwnd != IntPtr.Zero) Win32.DestroyWindow(hwnd);
        Console.WriteLine("PROBE-DONE");
    }
}

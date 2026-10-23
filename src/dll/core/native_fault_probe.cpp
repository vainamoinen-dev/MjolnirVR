// Crash diagnostics: a vectored exception handler that writes one record per faulting
// native address (registers, module, our live hooks, a raw stack scan). Started by dllmain.cpp,
// polled by game.cpp. Never handles or changes an exception; no heap or engine calls in the handler.
#include "native_fault_probe.h"
#include "../../common/log.h"
#include "title_adapter.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include "guard_count.h"

uint64_t Input_LastPadDeliveryMs() noexcept; // input.cpp, atomic loads
uint32_t Input_LastPadButtons() noexcept;
// hook_install_check.cpp (no MinHook header here: the probe test builds without it).
unsigned HookInstallCheck_LiveTargets(uintptr_t begin, uintptr_t end, uintptr_t* out, unsigned capacity) noexcept;
namespace
{
    // Records are immutable after ready. Multiple faulting threads claim distinct
    // slots without the log mutex, heap allocation or engine calls. One record per
    // instruction address bounds output from deliberately guarded native reads.
    struct FaultRecord
    {
        std::atomic<uintptr_t> address{};
        std::atomic<bool> ready{};
        char text[4096]{};
        bool reported{}; // worker only
    };
    FaultRecord records[64];
    HANDLE file = INVALID_HANDLE_VALUE;
    PVOID handler{};
    thread_local bool recording{};

    struct Text
    {
        char* bytes;
        size_t count{}, capacity;
        void Add(const char* s) noexcept
        {
            while (*s && count + 1 < capacity)
                bytes[count++] = *s++;
            bytes[count] = 0;
        }
        void Hex(uint64_t value) noexcept
        {
            Add("0x");
            for (int shift = 60; shift >= 0; shift -= 4)
            {
                const char digit[]{"0123456789ABCDEF"[(value >> shift) & 15], 0};
                Add(digit);
            }
        }
        void Address(uintptr_t address) noexcept
        {
            Hex(address);
            MEMORY_BASIC_INFORMATION info{};
            if (address && VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) &&
                info.Type == MEM_IMAGE)
            {
                Add(" image=");
                Hex(reinterpret_cast<uintptr_t>(info.AllocationBase));
                Add(" rva=");
                Hex(address - reinterpret_cast<uintptr_t>(info.AllocationBase));
            }
        }
    };

    // Faulting function [begin, end) from the image's unwind table, and the
    // image's [base, base+SizeOfImage). A lookup, never an unwind (see Capture).
    bool FaultRanges(uintptr_t ip, uintptr_t& functionBegin, uintptr_t& functionEnd, uintptr_t& imageBegin,
        uintptr_t& imageEnd) noexcept
    {
        functionBegin = functionEnd = imageBegin = imageEnd = 0;
        __try
        {
            DWORD64 base = 0;
            if (const auto* entry = RtlLookupFunctionEntry(ip, &base, nullptr))
            {
                functionBegin = static_cast<uintptr_t>(base + entry->BeginAddress);
                functionEnd = static_cast<uintptr_t>(base + entry->EndAddress);
            }
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(reinterpret_cast<void*>(ip), &info, sizeof(info)) && info.Type == MEM_IMAGE)
            {
                const auto* dos = static_cast<const IMAGE_DOS_HEADER*>(info.AllocationBase);
                if (dos->e_magic == IMAGE_DOS_SIGNATURE)
                {
                    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                        reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);
                    if (nt->Signature == IMAGE_NT_SIGNATURE)
                    {
                        imageBegin = reinterpret_cast<uintptr_t>(dos);
                        imageEnd = imageBegin + nt->OptionalHeader.SizeOfImage;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the faulting function's range faulted");
            return false;
        }
        return true;
    }

    bool Relevant(DWORD code) noexcept
    {
        // Do not treat C++ exceptions, debugger breakpoints or guard-page probes
        // as crashes. Stack-overflow handling cannot safely use this stack budget.
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INVALID_OPERATION:
            return true;
        default:
            return false;
        }
    }

    void Capture(EXCEPTION_POINTERS* exception)
    {
        const auto& native = *exception->ExceptionRecord;
        const auto& context = *exception->ContextRecord;
        const uintptr_t address = reinterpret_cast<uintptr_t>(native.ExceptionAddress);
        if (!address || !Relevant(native.ExceptionCode))
            return;
        FaultRecord* record = nullptr;
        for (auto& candidate : records)
        {
            uintptr_t expected = 0;
            if (candidate.address.compare_exchange_strong(expected, address, std::memory_order_acq_rel))
            {
                record = &candidate;
                break;
            }
            if (expected == address)
                return;
        }
        if (!record)
            return;
        Text out{record->text, 0, sizeof(record->text)};
        out.Add("NATIVE FIRST-CHANCE FAULT (may be caught by the game; not proof of a process crash)\r\n");
        out.Add("tick_ms=");
        out.Hex(GetTickCount64());
        out.Add(" thread=");
        out.Hex(GetCurrentThreadId());
        out.Add(" code=");
        out.Hex(native.ExceptionCode);
        out.Add(" flags=");
        out.Hex(native.ExceptionFlags);
        out.Add("\r\nip=");
        out.Address(address);
        if ((native.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || native.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
            native.NumberParameters >= 2)
        {
            out.Add(" access(0=read,1=write,8=execute)=");
            out.Hex(native.ExceptionInformation[0]);
            out.Add(" target=");
            out.Hex(native.ExceptionInformation[1]);
        }
        // Context for load-time crashes (atomic loads only): active title,
        // runtime mode (1 = loading) and when pad 0 last reached MCC.
        out.Add("\r\ntitle=");
        out.Hex(static_cast<uint64_t>(TitleAdapter_GetActiveTitle()));
        out.Add(" mode=");
        out.Hex(static_cast<uint64_t>(TitleAdapter_GetRuntimeMode()));
        out.Add(" pad_last_ms=");
        out.Hex(Input_LastPadDeliveryMs());
        out.Add(" pad_buttons=");
        out.Hex(Input_LastPadButtons());
        // Which of our live MinHook targets sit in the faulting function and
        // in its module (a detour patched into the code that faulted, or hooks
        // still live in a title that is tearing down).
        {
            uintptr_t functionBegin{}, functionEnd{}, imageBegin{}, imageEnd{};
            FaultRanges(address, functionBegin, functionEnd, imageBegin, imageEnd);
            uintptr_t targets[8]{};
            out.Add("\r\nhooks: function=");
            out.Hex(functionBegin);
            out.Add("-");
            out.Hex(functionEnd);
            const unsigned inFunction =
                functionBegin ? HookInstallCheck_LiveTargets(functionBegin, functionEnd, targets, 8) : 0;
            out.Add(" live_in_function=");
            out.Hex(inFunction);
            for (unsigned i = 0; i < inFunction && i < 8; ++i)
            {
                out.Add(" ");
                out.Address(targets[i]);
            }
            const unsigned inModule = imageBegin ? HookInstallCheck_LiveTargets(imageBegin, imageEnd, targets, 8) : 0;
            out.Add("\r\nhooks: module=");
            out.Hex(imageBegin);
            out.Add("-");
            out.Hex(imageEnd);
            out.Add(" live_in_module=");
            out.Hex(inModule);
            for (unsigned i = 0; i < inModule && i < 8; ++i)
            {
                out.Add(" rva=");
                out.Hex(targets[i] - imageBegin);
            }
        }
        out.Add("\r\nrsp=");
        out.Hex(context.Rsp);
        out.Add(" rbp=");
        out.Hex(context.Rbp);
        out.Add(" rcx=");
        out.Hex(context.Rcx);
        out.Add(" rdx=");
        out.Hex(context.Rdx);
        out.Add(" r8=");
        out.Hex(context.R8);
        out.Add(" r9=");
        out.Hex(context.R9);
        // Never unwind here. RtlCaptureStackBackTrace runs the system unwinder,
        // which __fastfail()s (uncatchable c0000409) on a frame whose function
        // tables are gone - exactly MCC's shutdown, when a caught first-chance
        // fault reached this handler and the walk killed the process (a crash dump
        // showed Capture -> RtlCaptureStackBackTrace -> fast fail). Instead scan the faulting stack itself:
        // values above the saved RSP, within this thread's own stack limits, that
        // point into executable image memory. Candidates, not an unwound chain.
        out.Add("\r\nstack scan (code addresses above RSP, candidates):\r\n");
        ULONG_PTR low = 0, high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        const uintptr_t rsp = static_cast<uintptr_t>(context.Rsp);
        if (rsp >= low && rsp < high && !(rsp & 7))
        {
            int shown = 0;
            const uintptr_t end = rsp + 0x2000 < high ? rsp + 0x2000 : high;
            for (uintptr_t at = rsp; at + 8 <= end && shown < 24; at += 8)
            {
                const uintptr_t value = *reinterpret_cast<const uintptr_t*>(at);
                MEMORY_BASIC_INFORMATION info{};
                if (value < 0x10000 || !VirtualQuery(reinterpret_cast<void*>(value), &info, sizeof(info)) ||
                    info.Type != MEM_IMAGE || info.State != MEM_COMMIT ||
                    !(info.Protect &
                        (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                    continue;
                out.Add("  ");
                out.Address(value);
                out.Add("\r\n");
                ++shown;
            }
        }
        else
            out.Add("  saved RSP is outside this thread's stack; address/registers above remain exact\r\n");
        out.Add("END FAULT\r\n");
        // The separate, preopened write-through file survives a process exit
        // before the worker can mirror the record into HaloMCCVR.log. No normal
        // firing/render hook performs I/O, and the handler never takes Logf's lock.
        DWORD written{};
        WriteFile(file, record->text, static_cast<DWORD>(out.count), &written, nullptr);
        record->ready.store(true, std::memory_order_release);
    }

    LONG CALLBACK Observe(EXCEPTION_POINTERS* exception)
    {
        if (recording || !exception || !exception->ExceptionRecord || !exception->ContextRecord)
            return EXCEPTION_CONTINUE_SEARCH;
        const DWORD lastError = GetLastError();
        recording = true;
        __try
        {
            Capture(exception);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("capturing a native fault for the probe faulted"); /* Only a diagnostic failure. */
        }
        recording = false;
        SetLastError(lastError);
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

bool NativeFaultProbe_Init(const wchar_t* directory, const char* source)
{
    if (handler)
        return true;
    const std::wstring path = std::wstring(directory) + L"HaloMCCVR-native-faults.log";
    MoveFileExW(path.c_str(), (path + L".prev").c_str(), MOVEFILE_REPLACE_EXISTING);
    file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    char header[512]{};
    Text out{header, 0, sizeof(header)};
    out.Add("HaloMCCVR native fault evidence; source=");
    out.Add(source);
    out.Add(" pid=");
    out.Hex(GetCurrentProcessId());
    out.Add("\r\n");
    DWORD written{};
    WriteFile(file, header, static_cast<DWORD>(out.count), &written, nullptr);
    handler = AddVectoredExceptionHandler(1, Observe);
    if (!handler)
    {
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
}

void NativeFaultProbe_Shutdown()
{
    // DLL detach: no handler of ours may outlive the module that holds it.
    if (handler)
    {
        RemoveVectoredExceptionHandler(handler);
        handler = nullptr;
    }
    if (file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
    }
}
void NativeFaultProbe_Poll()
{
    if (!handler)
        return;
    static uint64_t last{};
    const uint64_t now = GetTickCount64();
    if (last && now - last < 1000)
        return;
    last = now;
    // Module names/addresses are resolved on the worker, never by the handler.
    static constexpr const wchar_t* names[]{
        L"HaloMCCVR.dll", L"halo1.dll", L"halo2.dll", L"halo3.dll", L"halo3odst.dll", L"haloreach.dll", L"halo4.dll"};
    static HMODULE previous[7]{};
    for (size_t i = 0; i < 7; ++i)
    {
        const HMODULE module = GetModuleHandleW(names[i]);
        if (module != previous[i])
        {
            LOG("Native fault module map: %ls base=%p", names[i], module);
            previous[i] = module;
        }
    }
    for (auto& record : records)
        if (record.ready.load(std::memory_order_acquire) && !record.reported)
        {
            LOG("%s", record.text);
            record.reported = true;
        }
}

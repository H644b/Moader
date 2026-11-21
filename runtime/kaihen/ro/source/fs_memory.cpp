// SPDX-License-Identifier: GPL-2.0-only
#include "fs_memory.h"

#include <cstring>
#include <switch.h>
#include "../../common/ro_dmnt.h"
#include "session.h"
#include "wire.h"

namespace {

constexpr u64 FilesystemProgramId = 0x0400000000000000ULL;
constexpr u64 PageSize = 0x1000;
constexpr u64 MaximumReadSize = 0x400000;
constexpr u64 MaximumWriteSize = 256;
constexpr size_t MessageSize = 0x100;

/// Checks that @p process is FS with the requested process ID, and that the
/// range fits the caller's buffer and the command's limit.
Result checkFsRequest(Handle process, const RoProcessMemoryRequest& request, u64 limit, u64 capacity) {
    u64 process_id = 0;
    u64 program_id = 0;
    Result rc = svcGetProcessId(&process_id, process);
    if (R_SUCCEEDED(rc))
        rc = svcGetInfo(&program_id, InfoType_ProgramId, process, 0);
    if (R_FAILED(rc))
        return rc;

    if (program_id != FilesystemProgramId || request.process_id != process_id || !request.size ||
        request.size > limit || request.size > capacity || request.address > UINT64_MAX - request.size)
        return RoResult_BadRequest;
    return 0;
}

/// Checks that the range lies in one block of FS code memory: static or
/// mutable code for a read, writable mutable code for a write.
Result checkCodeBlock(Handle process, const RoProcessMemoryRequest& request, bool write) {
    MemoryInfo info = {};
    u32 page = 0;
    const Result rc = svcQueryProcessMemory(&info, &page, process, request.address);
    if (R_FAILED(rc))
        return rc;

    const bool inside = request.address >= info.addr && request.size <= info.size &&
                        request.address - info.addr <= info.size - request.size;
    const bool code = write ? (info.type == MemType_CodeMutable && (info.perm & Perm_W))
                            : (info.type == MemType_CodeStatic || info.type == MemType_CodeMutable);
    if (!inside || !code || info.attr)
        return RoResult_BadRequest;
    return 0;
}

/// The pages of a range, mapped into RO.
struct MappedRange {
    void* local;
    u64 page_address;
    u64 mapping_size;
    u8* bytes; ///< The range itself, within the mapped pages.
};

Result mapRange(Handle process, u64 address, u64 size, MappedRange* out) {
    const u64 page_address = address & ~(PageSize - 1);
    const u64 within_page = address - page_address;
    const u64 mapping_size = (within_page + size + PageSize - 1) & ~(PageSize - 1);

    virtmemLock();
    void* local = virtmemFindAslr(mapping_size, PageSize);
    if (!local) {
        virtmemUnlock();
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    const Result rc = svcMapProcessMemory(local, process, page_address, mapping_size);
    virtmemUnlock();
    if (R_FAILED(rc))
        return rc;

    *out = MappedRange{local, page_address, mapping_size, static_cast<u8*>(local) + within_page};
    return 0;
}

Result unmapRange(Handle process, const MappedRange& range) {
    return svcUnmapProcessMemory(range.local, process, range.page_address, range.mapping_size);
}

Result readFsMemory(Handle process, const RoProcessMemoryRequest& request, void* output, u64 capacity) {
    Result rc = checkFsRequest(process, request, MaximumReadSize, capacity);
    if (R_SUCCEEDED(rc))
        rc = checkCodeBlock(process, request, false);
    MappedRange range;
    if (R_SUCCEEDED(rc))
        rc = mapRange(process, request.address, request.size, &range);
    if (R_FAILED(rc))
        return rc;

    std::memcpy(output, range.bytes, request.size);
    return unmapRange(process, range);
}

Result writeFsMemory(Handle process, const RoProcessMemoryRequest& request, const void* input, u64 capacity) {
    Result rc = checkFsRequest(process, request, MaximumWriteSize, capacity);
    if (R_SUCCEEDED(rc))
        rc = checkCodeBlock(process, request, true);
    MappedRange range;
    if (R_SUCCEEDED(rc))
        rc = mapRange(process, request.address, request.size, &range);
    if (R_FAILED(rc))
        return rc;

    std::memcpy(range.bytes, input, request.size);
    armDCacheFlush(range.bytes, request.size);
    return unmapRange(process, range);
}

/// The CMIF header, or null when it does not fit with @p payload_size bytes
/// after it.
const CmifInHeader* getHeader(void* message, const HipcParsedRequest& request, size_t payload_size) {
    const u8* end = reinterpret_cast<const u8*>(request.data.data_words + request.meta.num_data_words);
    const auto* header = static_cast<const CmifInHeader*>(cmifGetAlignedDataStart(request.data.data_words, message));
    if (end > static_cast<const u8*>(message) + MessageSize ||
        reinterpret_cast<const u8*>(header + 1) + payload_size > end)
        return nullptr;
    return header;
}

/// True for a Request without a PID or descriptors other than the given buffers.
bool hasShape(const HipcParsedRequest& request, u32 copy_handles, u32 send_buffers, u32 recv_buffers) {
    const HipcMetadata& meta = request.meta;
    return meta.type == CmifCommandType_Request && !meta.send_pid &&
           meta.num_copy_handles == copy_handles && !meta.num_move_handles &&
           !meta.num_send_statics && meta.num_send_buffers == send_buffers &&
           meta.num_recv_buffers == recv_buffers && !meta.num_exch_buffers && !meta.num_recv_statics;
}

bool hasProbeShape(const HipcParsedRequest& request, bool pid, u32 copies, u32 receives) {
    const HipcMetadata& meta = request.meta;
    return meta.type == CmifCommandType_Request && meta.send_pid == pid &&
           meta.num_copy_handles == copies && !meta.num_move_handles &&
           !meta.num_send_statics && !meta.num_send_buffers &&
           meta.num_recv_buffers == receives && !meta.num_exch_buffers &&
           !meta.num_recv_statics;
}

void copyMemoryInfo(u64* out, const MemoryInfo& info) {
    out[0] = info.addr;
    out[1] = info.size;
    out[2] = info.type | (static_cast<u64>(info.attr) << 32);
    out[3] = info.perm | (static_cast<u64>(info.ipc_refcount) << 32);
    out[4] = info.device_refcount;
}

Result queryProcess(Handle process, u64 address, MemoryInfo* info) {
    u32 page = 0;
    if (process == CUR_PROCESS_HANDLE)
        return svcQueryMemory(info, &page, address);
    return svcQueryProcessMemory(info, &page, process, address);
}

// The process probe reports a free page in both address spaces. Never maps
// there until a later, separately checked command requests a mapping.
u64 freeAddress(Handle process, u64 size) {
    u64 start = 0, extent = 0, alias = 0, alias_size = 0, heap = 0, heap_size = 0;
    if (R_FAILED(svcGetInfo(&start, InfoType_AslrRegionAddress, process, 0)) ||
        R_FAILED(svcGetInfo(&extent, InfoType_AslrRegionSize, process, 0)) ||
        R_FAILED(svcGetInfo(&alias, InfoType_AliasRegionAddress, process, 0)) ||
        R_FAILED(svcGetInfo(&alias_size, InfoType_AliasRegionSize, process, 0)) ||
        R_FAILED(svcGetInfo(&heap, InfoType_HeapRegionAddress, process, 0)) ||
        R_FAILED(svcGetInfo(&heap_size, InfoType_HeapRegionSize, process, 0)) ||
        !extent || start > UINT64_MAX - extent || alias > UINT64_MAX - alias_size ||
        heap > UINT64_MAX - heap_size)
        return 0;
    const u64 end = start + extent;
    u64 at = start ? start : PageSize;
    for (u32 i = 0; i < 256 && at < end && size <= end - at; i++) {
        MemoryInfo info = {};
        if (R_FAILED(queryProcess(process, at, &info)) || !info.size ||
            info.addr > at || info.size > UINT64_MAX - info.addr ||
            info.addr + info.size <= at)
            return 0;
        const u64 next = info.addr + info.size;
        if (alias_size && at < alias + alias_size && at + size > alias) {
            at = alias + alias_size;
            continue;
        }
        if (heap_size && at < heap + heap_size && at + size > heap) {
            at = heap + heap_size;
            continue;
        }
        if (info.type == MemType_Unmapped && size <= next - at)
            return at;
        at = next;
    }
    return 0;
}

Result runProbeCommand(u32 command, Handle process, const u64* input, u32 bytes,
                       u64* out, u32* output_size, void* receive, u64 receive_size) {
    *output_size = 64;
    if (bytes < sizeof(RoProcessMemoryRequest) ||
        (receive_size && command != RoProcessReadCommand))
        return RoResult_BadRequest;
    u64 pid = 0, program = 0;
    const Result pid_rc = svcGetProcessId(&pid, process);
    const Result program_rc = svcGetInfo(&program, InfoType_ProgramId, process, 0);
    out[0] = pid;
    out[1] = program;
    out[2] = static_cast<u64>(pid_rc) | (static_cast<u64>(program_rc) << 32);
    if (R_FAILED(pid_rc)) return pid_rc;
    if (R_FAILED(program_rc)) return program_rc;
    if (input[0] && input[0] != pid) return RoResult_BadRequest;

    MemoryInfo info = {};
    const Result queried = queryProcess(process, input[1], &info);
    out[3] = queried;
    if (command == 0x524f0010) {
        copyMemoryInfo(out + 4, info);
        *output_size = 104;
        (void)svcGetInfo(&out[9], InfoType_AslrRegionAddress, process, 0);
        (void)svcGetInfo(&out[10], InfoType_AslrRegionSize, process, 0);
        out[11] = freeAddress(process, PageSize);
        out[12] = freeAddress(CUR_PROCESS_HANDLE, PageSize);
        return queried;
    }
    if (!input[0] || R_FAILED(queried))
        return R_FAILED(queried) ? queried : RoResult_BadRequest;

    if (command == 0x524f0011 || command == RoProcessReadCommand) {
        const u64 address = input[1], length = input[2];
        const bool bulk = command == RoProcessReadCommand;
        const u64 limit = bulk ? MaximumReadSize : 128;
        if (!length || length > limit || address > UINT64_MAX - length ||
            address < info.addr || length > info.size || address - info.addr > info.size - length ||
            (info.type != MemType_CodeStatic && info.type != MemType_CodeMutable) || info.attr ||
            (bulk && (!receive || length > receive_size)) ||
            (!bulk && (address & (PageSize - 1)) + length > PageSize))
            return RoResult_BadRequest;
        MappedRange range = {};
        const Result mapped = mapRange(process, address, length, &range);
        out[4] = mapped;
        if (R_FAILED(mapped)) return mapped;
        out[3] = reinterpret_cast<u64>(range.local);
        void* destination = bulk ? receive : static_cast<void*>(reinterpret_cast<u8*>(out + 7));
        std::memcpy(destination, range.bytes, length);
        out[5] = unmapRange(process, range);
        out[6] = length;
        *output_size = bulk ? 64 : static_cast<u32>(56 + length);
        return static_cast<Result>(out[5]);
    }
    if (command == 0x524f0012 || command == 0x524f0013) {
        const u64 source = input[1], destination = input[2], size = input[3];
        if (((source | destination) & (PageSize - 1)) || size != PageSize)
            return RoResult_BadRequest;
        if (command == 0x524f0013)
            return svcUnmapProcessCodeMemory(process, destination, source, size);
        if (info.type != MemType_Heap || info.perm != Perm_Rw || info.attr ||
            source < info.addr || source - info.addr > info.size - size)
            return RoResult_BadRequest;
        MemoryInfo target = {};
        const Result target_rc = queryProcess(process, destination, &target);
        if (R_FAILED(target_rc) || target.type != MemType_Unmapped ||
            destination < target.addr || size > target.size || destination - target.addr > target.size - size)
            return R_FAILED(target_rc) ? target_rc : RoResult_BadRequest;
        out[3] = svcMapProcessCodeMemory(process, destination, source, size);
        if (out[3]) return static_cast<Result>(out[3]);
        out[4] = svcSetProcessMemoryPermission(process, destination, size, Perm_Rx);
        if (out[4]) out[5] = svcUnmapProcessCodeMemory(process, destination, source, size);
        return static_cast<Result>(out[4]);
    }
    if (command == 0x524f0014) {
        *output_size = 80;
        const u64 address = input[1], size = input[2];
        if (bytes < 64 || size != 16 || (address & 3) ||
            (address & (PageSize - 1)) + size > PageSize ||
            !(info.perm & Perm_X) || info.attr || address < info.addr ||
            size > info.size || address - info.addr > info.size - size)
            return RoResult_BadRequest;
        MappedRange range;
        out[4] = mapRange(process, address, size, &range);
        if (out[4]) return static_cast<Result>(out[4]);
        out[9] = reinterpret_cast<u64>(range.local);
        MemoryInfo alias = {};
        const Result queried_alias = queryProcess(CUR_PROCESS_HANDLE,
            reinterpret_cast<u64>(range.local), &alias);
        out[5] = alias.perm | (static_cast<u64>(alias.attr) << 32);
        Result rc = queried_alias;
        if (R_SUCCEEDED(rc) && (alias.perm != Perm_Rw || alias.attr)) rc = RoResult_BadRequest;
        const u8* expected = reinterpret_cast<const u8*>(input + 4);
        const u8* replacement = reinterpret_cast<const u8*>(input + 6);
        if (R_SUCCEEDED(rc)) {
            out[6] = std::memcmp(range.bytes, expected, size) == 0;
            if (!out[6]) rc = RoResult_BadRequest;
        }
        if (R_SUCCEEDED(rc)) {
            std::memcpy(range.bytes, replacement, size);
            armDCacheClean(range.bytes, size);
            armICacheInvalidate(range.bytes, size);
            out[7] = std::memcmp(range.bytes, replacement, size) == 0;
            if (!out[7]) rc = RoResult_BadRequest;
        }
        out[8] = unmapRange(process, range);
        return out[8] ? static_cast<Result>(out[8]) : rc;
    }
    if (command == 0x524f0015) {
        const u64 address = input[1], size = input[2], permission = input[3];
        if (program != FilesystemProgramId || ((address | size) & (PageSize - 1)) ||
            !size || size > 0x4000000 ||
            (info.type != MemType_CodeStatic && info.type != MemType_CodeMutable) || info.attr ||
            (info.perm != Perm_X && info.perm != Perm_Rx) ||
            (permission != Perm_X && permission != Perm_Rx) || address < info.addr ||
            size > info.size || address - info.addr > info.size - size)
            return RoResult_BadRequest;
        out[3] = info.perm;
        out[4] = svcSetProcessMemoryPermission(process, address, size, permission);
        if (out[4]) return static_cast<Result>(out[4]);
        MemoryInfo after = {};
        out[5] = queryProcess(process, address, &after);
        copyMemoryInfo(out + 6, after);
        *output_size = 88;
        return static_cast<Result>(out[5]);
    }
    return RoResult_BadRequest;
}

/// Moader's source-qualified Seedsow proof uses the same private service as
/// HEN, with a kernel PID descriptor on every diagnostic reply. HEN's older
/// read/write shape has no incoming PID and keeps its original reply layout.
bool serveProbe(void* message, const HipcParsedRequest& request) {
    const CmifInHeader* header = getHeader(message, request, 0);
    if (!header || header->magic != CMIF_IN_HEADER_MAGIC)
        return false;
    const u32 command = header->command_id;
    if (command == 0x524f0001) {
        if (!hasProbeShape(request, false, 0, 0) ||
            !getHeader(message, request, sizeof(u64))) {
            roWriteDiagnosticReply(message, RoResult_BadRequest, nullptr, 0);
            return true;
        }
        u64 nonce = 0, pid = 0, thread = 0;
        std::memcpy(&nonce, header + 1, sizeof(nonce));
        const Result pid_rc = svcGetProcessId(&pid, CUR_PROCESS_HANDLE);
        const Result thread_rc = svcGetThreadId(&thread, CUR_THREAD_HANDLE);
        const u64 reply[6] = {0x4558454f5232534eULL, nonce ^ 0x9d7c3a51e6b2048fULL,
                              pid, thread, svcGetSystemTick(), 0};
        roWriteDiagnosticReply(message, R_FAILED(pid_rc) ? pid_rc : thread_rc,
                               reply, sizeof(reply));
        return true;
    }
    if (command < 0x524f0010 || command > RoProcessReadCommand ||
        (command == RoProcessReadCommand && !request.meta.send_pid))
        return false;
    const u32 bytes = command == 0x524f0014 ? 64 : sizeof(RoProcessMemoryRequest);
    if (!hasProbeShape(request, true, 1, command == RoProcessReadCommand ? 1 : 0) ||
        !getHeader(message, request, bytes) ||
        request.data.copy_handles[0] == INVALID_HANDLE) {
        roWriteDiagnosticReply(message, RoResult_BadRequest, nullptr, 0);
        return true;
    }
    u64 input[8] = {};
    std::memcpy(input, header + 1, bytes);
    u64 output[24] = {};
    u32 size = 64;
    void* receive = command == RoProcessReadCommand ?
        hipcGetBufferAddress(&request.data.recv_buffers[0]) : nullptr;
    const u64 capacity = command == RoProcessReadCommand ?
        hipcGetBufferSize(&request.data.recv_buffers[0]) : 0;
    const Result rc = runProbeCommand(command, request.data.copy_handles[0],
                                      input, bytes, output, &size, receive, capacity);
    roWriteDiagnosticReply(message, rc, output, size);
    return true;
}

/// Answers RoDiagnosticsCommand, if that is the request.
bool serveDiagnostics(void* message, const HipcParsedRequest& request) {
    const CmifInHeader* header = getHeader(message, request, 0);
    if (!header || !hasShape(request, 0, 0, 1) || header->magic != CMIF_IN_HEADER_MAGIC ||
        header->command_id != RoDiagnosticsCommand)
        return false;

    const RoDiagnostics diagnostics = roGetDiagnostics();
    void* output = hipcGetBufferAddress(&request.data.recv_buffers[0]);
    const u64 capacity = hipcGetBufferSize(&request.data.recv_buffers[0]);
    Result rc = RoResult_BadRequest;
    if (output && capacity >= sizeof(diagnostics)) {
        std::memcpy(output, &diagnostics, sizeof(diagnostics));
        rc = 0;
    }
    roWriteReply(message, rc, 0, false);
    return true;
}

/// Runs a read or write request, or returns RoResult_BadRequest for any other.
Result runMemoryCommand(void* message, const HipcParsedRequest& request) {
    const CmifInHeader* header = getHeader(message, request, sizeof(RoProcessMemoryRequest));
    if (!header || header->magic != CMIF_IN_HEADER_MAGIC)
        return RoResult_BadRequest;

    const bool read = header->command_id == RoProcessReadCommand;
    const bool write = header->command_id == RoProcessWriteCommand;
    if ((!read && !write) || !hasShape(request, 1, write ? 1 : 0, read ? 1 : 0))
        return RoResult_BadRequest;

    const Handle process = request.data.copy_handles[0];
    if (process == INVALID_HANDLE)
        return RoResult_BadRequest;

    RoProcessMemoryRequest args;
    std::memcpy(&args, header + 1, sizeof(args));
    if (read) {
        return readFsMemory(process, args, hipcGetBufferAddress(&request.data.recv_buffers[0]),
                            hipcGetBufferSize(&request.data.recv_buffers[0]));
    }
    return writeFsMemory(process, args, hipcGetBufferAddress(&request.data.send_buffers[0]),
                         hipcGetBufferSize(&request.data.send_buffers[0]));
}

void closeRequestHandles(const HipcParsedRequest& request) {
    for (u32 i = 0; i < request.meta.num_copy_handles; i++) {
        if (request.data.copy_handles[i] != INVALID_HANDLE)
            svcCloseHandle(request.data.copy_handles[i]);
    }
    for (u32 i = 0; i < request.meta.num_move_handles; i++) {
        if (request.data.move_handles[i] != INVALID_HANDLE)
            svcCloseHandle(request.data.move_handles[i]);
    }
}

} // namespace

void roFsMemoryDispatch(void* message, bool fs_only) {
    if (roReplyQueryPointerBufferSize(message))
        return;

    const HipcParsedRequest request = hipcParseRequest(message);
    if (fs_only) {
        const CmifInHeader* header = getHeader(message, request, 0);
        const u32 command = header ? header->command_id : 0;
        const Handle process = request.meta.num_copy_handles == 1 ?
            request.data.copy_handles[0] : INVALID_HANDLE;
        u64 program = 0;
        const bool standalone_command = command == 0x524f0010 ||
            command == 0x524f0012 || command == 0x524f0014 ||
            command == 0x524f0015 || command == RoProcessReadCommand;
        if (!header || header->magic != CMIF_IN_HEADER_MAGIC ||
            !standalone_command ||
            !request.meta.send_pid || process == INVALID_HANDLE ||
            R_FAILED(svcGetInfo(&program, InfoType_ProgramId, process, 0)) ||
            program != FilesystemProgramId) {
            closeRequestHandles(request);
            roWriteDiagnosticReply(message, RoResult_BadRequest, nullptr, 0);
            return;
        }
        if (serveProbe(message, request)) {
            closeRequestHandles(request);
            return;
        }
        closeRequestHandles(request);
        roWriteDiagnosticReply(message, RoResult_BadRequest, nullptr, 0);
        return;
    }
    if (serveDiagnostics(message, request))
        return;

    if (serveProbe(message, request)) {
        closeRequestHandles(request);
        return;
    }

    const Result rc = runMemoryCommand(message, request);
    closeRequestHandles(request);
    roWriteReply(message, rc, 0, false);
}

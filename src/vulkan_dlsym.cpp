// Hands our vkGetInstanceProcAddr to code that asks the Vulkan loader's
// handle for it, as bg3 and SDL do. A handle-scoped dlsym never searches
// LD_PRELOAD objects; our exports were reached only when the loader resolved
// its own entry points through its GOT, which Ubuntu's -Bsymbolic-functions
// build does not. Connor Murphy found the lookup (#10); thank you.
//
// Exported as dlsym for every caller. Anything we don't answer tail-jumps to
// libc's, so glibc still sees the original caller and RTLD_NEXT is unchanged.

#include <dlfcn.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <cstring>

#include "log.h"

namespace {

using Dlsym = void* (*)(void*, char const*);

struct Route {
    void* answer;  // returned when set
    void* next;    // libc's dlsym, jumped to otherwise
};

Dlsym libc_dlsym() {
    static std::atomic<Dlsym> fn{nullptr};
    Dlsym f = fn.load(std::memory_order_acquire);
    if (f == nullptr) {
        // GLIBC_2.2.5 exists in every x86_64 glibc; 2.34 kept it as a compat alias.
        f = (Dlsym)::dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
        fn.store(f, std::memory_order_release);
    }
    return f;
}

char const* base_name(char const* path) {
    if (path == nullptr || path[0] == '\0') return "the executable";
    char const* slash = std::strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

// Only when the lookup lands on the loader we chain to: a layer's or a
// driver's own vkGetInstanceProcAddr is theirs.
void* answer(void* handle, char const* name, void* caller, Dlsym next) {
    if (handle == RTLD_DEFAULT || handle == RTLD_NEXT || name == nullptr
        || std::strcmp(name, "vkGetInstanceProcAddr") != 0) {
        return nullptr;
    }
    void* found = next(handle, name);
    void* loader = next(RTLD_NEXT, name);
    void* ours = (void*)&vkGetInstanceProcAddr;
    if (found == nullptr || found != loader || found == ours) return nullptr;

    static std::atomic<int> said{0};
    if (said.fetch_add(1, std::memory_order_relaxed) < 4) {
        Dl_info who{};
        ::dladdr(caller, &who);
        bg3le::logf("vulkan: %s asked the loader for vkGetInstanceProcAddr; answering with ours",
                    base_name(who.dli_fname));
    }
    return ours;
}

}  // namespace

extern "C" __attribute__((used, visibility("hidden")))
Route bg3le_dlsym_route(void* handle, char const* name, void* caller) {
    Dlsym next = libc_dlsym();
    return {answer(handle, name, caller, next), (void*)next};
}

// dlsym(handle, name): ask the route with the caller's return address, then
// return its answer or jump to libc's dlsym with the original stack.
asm(R"(
    .pushsection .text
    .globl dlsym
    .type dlsym, @function
    .p2align 4
dlsym:
    .cfi_startproc
    endbr64
    push %rdi
    .cfi_adjust_cfa_offset 8
    push %rsi
    .cfi_adjust_cfa_offset 8
    sub $8, %rsp
    .cfi_adjust_cfa_offset 8
    mov 24(%rsp), %rdx
    call bg3le_dlsym_route
    add $8, %rsp
    .cfi_adjust_cfa_offset -8
    pop %rsi
    .cfi_adjust_cfa_offset -8
    pop %rdi
    .cfi_adjust_cfa_offset -8
    test %rax, %rax
    jz 1f
    ret
1:
    jmp *%rdx
    .cfi_endproc
    .size dlsym, . - dlsym
    .popsection
)");

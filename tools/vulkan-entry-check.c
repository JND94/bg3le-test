// The game's way of finding Vulkan, against bg3le's dlsym (src/vulkan_dlsym.cpp).
// tools/check-vulkan-entry.sh builds this three ways and runs it.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef void (*Fn)(void);

#if defined(STANDIN_LOADER)

// The loader's global entry points, nothing more. Built plain and with
// -Bsymbolic-functions, the two ways distributions build the real one.
int vkCreateInstance(void const* info, void const* alloc, void* out) {
    (void)info; (void)alloc; (void)out;
    return 0;
}

Fn vkGetInstanceProcAddr(void* inst, char const* name) {
    (void)inst;
    if (strcmp(name, "vkCreateInstance") == 0) return (Fn)vkCreateInstance;
    if (strcmp(name, "vkGetInstanceProcAddr") == 0) return (Fn)vkGetInstanceProcAddr;
    return NULL;
}

#elif defined(STANDIN_LAYER)

// A layer's or driver's own vkGetInstanceProcAddr, which must stay its own.
Fn vkGetInstanceProcAddr(void* inst, char const* name) {
    (void)inst; (void)name;
    return NULL;
}

#else

typedef Fn (*Gipa)(void*, char const*);

static int failures;

static char const* owner(void const* p) {
    Dl_info i;
    if (p == NULL) return "(null)";
    if (dladdr(p, &i) == 0 || i.dli_fname == NULL) return "(unknown)";
    return i.dli_fname;
}

static void expect(char const* what, void const* p, char const* in) {
    char const* who = owner(p);
    int ok = strstr(who, in) != NULL;
    printf("  %s  %-46s %s\n", ok ? "ok  " : "FAIL", what, who);
    failures += !ok;
}

// bg3 defines its own and finds the next one with RTLD_NEXT.
int pthread_create(void* t, void const* attr, void* (*fn)(void*), void* arg) {
    typedef int (*Real)(void*, void const*, void* (*)(void*), void*);
    Real real = (Real)dlsym(RTLD_NEXT, "pthread_create");
    return real(t, attr, fn, arg);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s path/to/standin-layer.so\n", argv[0]);
        return 2;
    }

    // As bg3+0x36b5eb0 does it.
    void* h = dlopen("libvulkan.so", RTLD_NOW);
    if (h == NULL) h = dlopen("libvulkan.so.1", RTLD_NOW);
    if (h == NULL) {
        printf("  FAIL  no loader: %s\n", dlerror());
        return 1;
    }
    Gipa gipa = (Gipa)dlsym(h, "vkGetInstanceProcAddr");
    expect("dlsym(loader, vkGetInstanceProcAddr)", (void*)gipa, "libbg3le");
    if (gipa != NULL) {
        expect("  -> (NULL, vkCreateInstance)", (void*)gipa(NULL, "vkCreateInstance"), "libbg3le");
        expect("  -> (NULL, vkGetInstanceProcAddr)", (void*)gipa(NULL, "vkGetInstanceProcAddr"), "libbg3le");
    }

    expect("dlsym(loader, vkCreateInstance)", dlsym(h, "vkCreateInstance"), "libvulkan");
    expect("dlsym(RTLD_NEXT, pthread_create)", dlsym(RTLD_NEXT, "pthread_create"), "libbg3le");

    void* layer = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    expect("dlsym(layer, vkGetInstanceProcAddr)",
           layer != NULL ? dlsym(layer, "vkGetInstanceProcAddr") : NULL, "standin-layer");

    dlerror();
    void* none = dlsym(h, "bg3le_no_such_symbol");
    int reported = none == NULL && dlerror() != NULL;
    printf("  %s  %-46s %s\n", reported ? "ok  " : "FAIL", "a missing symbol", reported ? "null, dlerror set" : "wrong");
    failures += !reported;

    return failures == 0 ? 0 : 1;
}

#endif

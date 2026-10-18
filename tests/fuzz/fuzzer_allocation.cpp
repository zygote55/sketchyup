// Linked only into the libFuzzer targets (D-022, R083.i).
//
// The Clang libFuzzer runtime (libclang_rt.fuzzer) carries a private libc++ whose
// global operator new and delete are weak and exported. The driver links that
// archive before the ASan C++ runtime, so the throwing new, the array and aligned
// new, and every plain, sized and aligned delete in the target resolve to
// libFuzzer's copies, which call malloc and free. Its nothrow new is local, so the
// nothrow new and delete resolve to ASan's interceptors instead. Code that pairs a
// nothrow new with an ordinary delete, as libstdc++'s std::stable_sort buffer does
// inside Qt's JSON parser, then frees through free memory that ASan recorded as
// operator new, and ASan reports alloc-dealloc-mismatch.
//
// Defining the nothrow forms here routes them to the same operator new and delete
// as the rest of the target, so every allocation and release in the target uses
// one family and the check stays enabled.
#include <cstddef>
#include <new>

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    try {
        return ::operator new[](size);
    } catch (...) {
        return nullptr;
    }
}
void *operator new(std::size_t size, std::align_val_t align, const std::nothrow_t &) noexcept {
    try {
        return ::operator new(size, align);
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t &) noexcept {
    try {
        return ::operator new[](size, align);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void *pointer, const std::nothrow_t &) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, const std::nothrow_t &) noexcept { ::operator delete[](pointer); }
void operator delete(void *pointer, std::align_val_t align, const std::nothrow_t &) noexcept {
    ::operator delete(pointer, align);
}
void operator delete[](void *pointer, std::align_val_t align, const std::nothrow_t &) noexcept {
    ::operator delete[](pointer, align);
}

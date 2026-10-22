#pragma once
// An optional value kept on the heap, with std::optional's value semantics: copies are deep,
// == compares values, and the value is created and destroyed with the holder. Unlike
// std::optional, a type that holds one needs only a declaration of T. The copy, destroy and
// compare operations are captured where a value is stored (where T is complete), so a widely
// included record such as Body can hold a large, often-edited type without including its
// header (build fan-out, ADR 0170). Reading the value needs T's definition.
#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>
namespace sketchy {
template <class T> class IndirectOptional {
    struct Operations {
        T *(*copy)(const T &);
        void (*destroy)(T *);
        bool (*equal)(const T &, const T &);
    };
    static const Operations *operations() {
        static constexpr Operations instance{[](const T &value) { return new T(value); },
                                             [](T *value) { delete value; },
                                             [](const T &a, const T &b) { return a == b; }};
        return &instance;
    }
    T *value_{};
    const Operations *operations_{};
    void adopt(T *value) {
        reset();
        value_ = value;
        operations_ = operations();
    }

  public:
    using value_type = T;
    IndirectOptional() noexcept = default;
    IndirectOptional(std::nullopt_t) noexcept {}
    // The converting members are templates constrained on T itself, so that overload
    // resolution against other types (std::nullopt, say) never needs T's definition.
    template <class U>
        requires std::same_as<std::remove_cvref_t<U>, T>
    IndirectOptional(U &&value)
        : value_(new T(std::forward<U>(value))), operations_(operations()) {}
    template <class U>
        requires std::same_as<U, T>
    IndirectOptional(const std::optional<U> &value) {
        if (value)
            adopt(new T(*value));
    }
    template <class U>
        requires std::same_as<U, T>
    IndirectOptional(std::optional<U> &&value) {
        if (value)
            adopt(new T(std::move(*value)));
    }
    IndirectOptional(const IndirectOptional &other)
        : value_(other.value_ ? other.operations_->copy(*other.value_) : nullptr),
          operations_(other.operations_) {}
    IndirectOptional(IndirectOptional &&other) noexcept
        : value_(std::exchange(other.value_, nullptr)), operations_(other.operations_) {}
    ~IndirectOptional() { reset(); }
    IndirectOptional &operator=(const IndirectOptional &other) {
        if (this != &other) {
            T *copy = other.value_ ? other.operations_->copy(*other.value_) : nullptr;
            reset();
            value_ = copy;
            operations_ = other.operations_;
        }
        return *this;
    }
    IndirectOptional &operator=(IndirectOptional &&other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
            operations_ = other.operations_;
        }
        return *this;
    }
    IndirectOptional &operator=(std::nullopt_t) noexcept {
        reset();
        return *this;
    }
    template <class U>
        requires std::same_as<std::remove_cvref_t<U>, T>
    IndirectOptional &operator=(U &&value) {
        if (value_)
            *value_ = std::forward<U>(value);
        else
            adopt(new T(std::forward<U>(value)));
        return *this;
    }
    template <class U>
        requires std::same_as<U, T>
    IndirectOptional &operator=(const std::optional<U> &value) {
        if (value)
            *this = *value;
        else
            reset();
        return *this;
    }
    template <class... Args> T &emplace(Args &&...args) {
        adopt(new T(std::forward<Args>(args)...));
        return *value_;
    }
    void reset() noexcept {
        if (value_)
            operations_->destroy(value_);
        value_ = nullptr;
    }
    bool has_value() const noexcept { return value_ != nullptr; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
    T &operator*() & { return *value_; }
    const T &operator*() const & { return *value_; }
    T *operator->() { return value_; }
    const T *operator->() const { return value_; }
    T &value() & {
        if (!value_)
            throw std::bad_optional_access();
        return *value_;
    }
    const T &value() const & {
        if (!value_)
            throw std::bad_optional_access();
        return *value_;
    }
    template <class U>
        requires std::same_as<U, T>
    operator std::optional<U>() const {
        return value_ ? std::optional<U>(*value_) : std::nullopt;
    }
    friend bool operator==(const IndirectOptional &a, const IndirectOptional &b) {
        if (!a.value_ || !b.value_)
            return !a.value_ && !b.value_;
        return a.operations_->equal(*a.value_, *b.value_);
    }
    friend bool operator==(const IndirectOptional &a, std::nullopt_t) noexcept { return !a.value_; }
};
} // namespace sketchy

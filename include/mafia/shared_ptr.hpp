#pragma once

// Собственная реализация умного указателя с разделяемым владением
// (аналог std::shared_ptr без weak_ptr и aliasing-конструктора).

#include <atomic>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace mafia {

template <class T>
struct DefaultDelete {
    void operator()(T* ptr) const noexcept { delete ptr; }
};

namespace detail {

// Управляющий блок: атомарный счётчик владельцев и знание о том,
// как уничтожить управляемый объект.
class ControlBlockBase {
public:
    ControlBlockBase() noexcept = default;
    ControlBlockBase(const ControlBlockBase&) = delete;
    ControlBlockBase& operator=(const ControlBlockBase&) = delete;

    // Новый владелец появляется только из уже существующего,
    // поэтому упорядочивание не требуется.
    void addRef() noexcept { refs_.fetch_add(1, std::memory_order_relaxed); }

    // acq_rel: все записи в объект, сделанные владельцами в других нитях,
    // видны нити, которая уничтожает объект последней.
    void release() noexcept {
        if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            destroyObject();
            delete this;
        }
    }

    long useCount() const noexcept { return refs_.load(std::memory_order_relaxed); }

protected:
    virtual ~ControlBlockBase() = default;

private:
    virtual void destroyObject() noexcept = 0;

    std::atomic<long> refs_{1};
};

// Блок для объекта, созданного отдельно (SharedPtr(new U, deleter)).
// Хранит указатель исходного типа U, поэтому SharedPtr<Base>, полученный
// из SharedPtr<Derived>, удалит объект как Derived.
template <class U, class Deleter>
class PointerControlBlock final : public ControlBlockBase {
public:
    PointerControlBlock(U* ptr, const Deleter& deleter) : ptr_(ptr), deleter_(deleter) {}

private:
    void destroyObject() noexcept override { deleter_(ptr_); }

    U* ptr_;
    Deleter deleter_;
};

// Блок для MakeShared: объект живёт внутри блока, одна аллокация на всё.
template <class U>
class InplaceControlBlock final : public ControlBlockBase {
public:
    template <class... Args>
    explicit InplaceControlBlock(Args&&... args) {
        ::new (static_cast<void*>(storage_)) U(std::forward<Args>(args)...);
    }

    U* object() noexcept { return std::launder(reinterpret_cast<U*>(storage_)); }

private:
    void destroyObject() noexcept override { object()->~U(); }

    alignas(U) unsigned char storage_[sizeof(U)];
};

}  // namespace detail

template <class From, class To>
concept PointerConvertible = std::convertible_to<From*, To*>;

template <class T>
class SharedPtr {
public:
    using element_type = T;

    constexpr SharedPtr() noexcept = default;
    constexpr SharedPtr(std::nullptr_t) noexcept {}

    template <class U>
        requires PointerConvertible<U, T>
    explicit SharedPtr(U* ptr) : SharedPtr(ptr, DefaultDelete<U>{}) {}

    // Если выделить управляющий блок не удалось, объект удаляется,
    // как и у std::shared_ptr.
    template <class U, class Deleter>
        requires PointerConvertible<U, T> && std::invocable<Deleter&, U*>
    SharedPtr(U* ptr, Deleter deleter) {
        try {
            control_ = new detail::PointerControlBlock<U, Deleter>(ptr, deleter);
        } catch (...) {
            deleter(ptr);
            throw;
        }
        ptr_ = ptr;
    }

    SharedPtr(const SharedPtr& other) noexcept : ptr_(other.ptr_), control_(other.control_) {
        acquire();
    }

    template <class U>
        requires PointerConvertible<U, T>
    SharedPtr(const SharedPtr<U>& other) noexcept : ptr_(other.ptr_), control_(other.control_) {
        acquire();
    }

    SharedPtr(SharedPtr&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr)), control_(std::exchange(other.control_, nullptr)) {}

    template <class U>
        requires PointerConvertible<U, T>
    SharedPtr(SharedPtr<U>&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr)), control_(std::exchange(other.control_, nullptr)) {}

    ~SharedPtr() {
        if (control_ != nullptr) {
            control_->release();
        }
    }

    // Присваивания через copy-and-swap: корректны и при самоприсваивании.
    SharedPtr& operator=(const SharedPtr& other) noexcept {
        SharedPtr(other).swap(*this);
        return *this;
    }

    template <class U>
        requires PointerConvertible<U, T>
    SharedPtr& operator=(const SharedPtr<U>& other) noexcept {
        SharedPtr(other).swap(*this);
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& other) noexcept {
        SharedPtr(std::move(other)).swap(*this);
        return *this;
    }

    template <class U>
        requires PointerConvertible<U, T>
    SharedPtr& operator=(SharedPtr<U>&& other) noexcept {
        SharedPtr(std::move(other)).swap(*this);
        return *this;
    }

    SharedPtr& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    void reset() noexcept { SharedPtr().swap(*this); }

    template <class U>
        requires PointerConvertible<U, T>
    void reset(U* ptr) {
        SharedPtr(ptr).swap(*this);
    }

    template <class U, class Deleter>
        requires PointerConvertible<U, T> && std::invocable<Deleter&, U*>
    void reset(U* ptr, Deleter deleter) {
        SharedPtr(ptr, std::move(deleter)).swap(*this);
    }

    void swap(SharedPtr& other) noexcept {
        std::swap(ptr_, other.ptr_);
        std::swap(control_, other.control_);
    }

    T* get() const noexcept { return ptr_; }

    std::add_lvalue_reference_t<T> operator*() const noexcept
        requires(!std::is_void_v<T>)
    {
        return *ptr_;
    }

    T* operator->() const noexcept { return ptr_; }

    long use_count() const noexcept { return control_ != nullptr ? control_->useCount() : 0; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    template <class U>
    friend class SharedPtr;

    template <class U, class... Args>
    friend SharedPtr<U> MakeShared(Args&&... args);

    // Принимает уже созданный управляющий блок (используется MakeShared).
    SharedPtr(T* ptr, detail::ControlBlockBase* control) noexcept : ptr_(ptr), control_(control) {}

    void acquire() noexcept {
        if (control_ != nullptr) {
            control_->addRef();
        }
    }

    T* ptr_ = nullptr;
    detail::ControlBlockBase* control_ = nullptr;
};

template <class T, class... Args>
SharedPtr<T> MakeShared(Args&&... args) {
    auto* control = new detail::InplaceControlBlock<T>(std::forward<Args>(args)...);
    return SharedPtr<T>(control->object(), control);
}

template <class T>
void swap(SharedPtr<T>& lhs, SharedPtr<T>& rhs) noexcept {
    lhs.swap(rhs);
}

// Сравнения — по хранимому адресу; !=, <, <=, >, >= выводятся компилятором (C++20).
template <class T, class U>
bool operator==(const SharedPtr<T>& lhs, const SharedPtr<U>& rhs) noexcept {
    return lhs.get() == rhs.get();
}

template <class T, class U>
std::strong_ordering operator<=>(const SharedPtr<T>& lhs, const SharedPtr<U>& rhs) noexcept {
    return std::compare_three_way{}(lhs.get(), rhs.get());
}

template <class T>
bool operator==(const SharedPtr<T>& lhs, std::nullptr_t) noexcept {
    return !lhs;
}

template <class T>
std::strong_ordering operator<=>(const SharedPtr<T>& lhs, std::nullptr_t) noexcept {
    return std::compare_three_way{}(lhs.get(), static_cast<T*>(nullptr));
}

}  // namespace mafia

template <class T>
struct std::hash<mafia::SharedPtr<T>> {
    std::size_t operator()(const mafia::SharedPtr<T>& ptr) const noexcept {
        return std::hash<T*>{}(ptr.get());
    }
};

//
// Created by cycastic on 8/13/23.
//

#ifndef MICROJIT_HELPER_H
#define MICROJIT_HELPER_H

#include "def.h"
#include "safe_refcount.h"

namespace microjit {
    class ThreadUnsafeObject {
    private:
        mutable unsigned int refcount{};
    public:
        void init_ref() const { refcount = 1; }
        bool ref() const { return (refcount == 0 ? 0 : ++refcount) != 0; }
        bool unref() const { return --refcount == 0; }
        unsigned int get_reference_count() const { return refcount; }
        virtual ~ThreadUnsafeObject() = default;
    };
    class ThreadSafeObject {
    private:
        mutable SafeRefCount refcount{};
    public:
        void init_ref() const { refcount.init(); }
        bool ref() const { return refcount.ref(); }
        bool unref() const { return refcount.unref(); }
        unsigned int get_reference_count() const { return refcount.get(); }
        virtual ~ThreadSafeObject() = default;
    };
    template <class T>
    class Ref {
        T* reference{};

        void ref(const Ref& p_from) {
            if (p_from.reference == reference) return;
            unref();
            reference = p_from.reference;
            if (reference) reference->ref();
        }
    public:
        void unref() {
            if (reference && reference->unref()) {
                delete reference;
                reference = nullptr;
            }
        }
        _ALWAYS_INLINE_ bool operator==(const T *p_ptr) const {
            return reference == p_ptr;
        }
        _ALWAYS_INLINE_ bool operator!=(const T *p_ptr) const {
            return reference != p_ptr;
        }

        _ALWAYS_INLINE_ bool operator<(const Ref<T> &p_r) const {
            return reference < p_r.reference;
        }
        _ALWAYS_INLINE_ bool operator==(const Ref<T> &p_r) const {
            return reference == p_r.reference;
        }
        _ALWAYS_INLINE_ bool operator!=(const Ref<T> &p_r) const {
            return reference != p_r.reference;
        }

        _ALWAYS_INLINE_ T *operator->() {
            return reference;
        }

        _ALWAYS_INLINE_ T *operator*() {
            return reference;
        }

        _ALWAYS_INLINE_ const T *operator->() const {
            return reference;
        }

        _ALWAYS_INLINE_ const T *ptr() const {
            return reference;
        }
        _ALWAYS_INLINE_ T *ptr() {
            return reference;
        }

        _ALWAYS_INLINE_ const T *operator*() const {
            return reference;
        }

        Ref& operator=(const Ref &p_from) {
            ref(p_from);
            return *this;
        }
        Ref(const Ref &p_from) {
            ref(p_from);
        }
        _NO_DISCARD_ _ALWAYS_INLINE_ bool is_valid() const { return reference != nullptr; }
        _NO_DISCARD_ _ALWAYS_INLINE_ bool is_null() const { return reference == nullptr; }

        Ref() = default;

        ~Ref() {
            unref();
        }

        static _ALWAYS_INLINE_ Ref<T> from_initialized_object(T* p_ptr){
            Ref<T> re{};
            if (!p_ptr) return re;
            if (!p_ptr->ref()) return re;
            re.reference = p_ptr;
            return re;
        }
        static _ALWAYS_INLINE_ Ref<T> from_uninitialized_object(T* p_ptr){
            Ref<T> re{};
            if (!p_ptr) return re;
            p_ptr->init_ref();
            re.reference = p_ptr;
            return re;
        }
        template<class... Args >
        static _ALWAYS_INLINE_ Ref<T> make_ref(Args&&... args){
            return from_uninitialized_object(new T(args...));
        }
        static _ALWAYS_INLINE_ Ref<T> null() { return Ref<T>(); }

        template <class To>
        _ALWAYS_INLINE_ Ref<To> safe_cast() const {
            auto casted = Ref<To>::from_initialized_object(dynamic_cast<To*>(reference));
            return casted;
        }
        template <class To>
        _ALWAYS_INLINE_ Ref<To> c_style_cast() const {
            // Ref<T> only hold a single property, a reference to an object
            // So assuming that the programmer know what they are doing, this should not cause any problem
            auto casted = (Ref<To>*)this;
            return *casted;
        }
    };
    template <typename T, class RefCounter = ThreadUnsafeObject>
    class Box {
    private:
        class InnerPointer : public RefCounter {
            T data;
        public:
            template<class... Args >
            explicit InnerPointer(Args&& ...args) : data(args...) {}

            friend class Box<T, RefCounter>;
        };
        Ref<InnerPointer> inner_ptr{};

    public:
        _ALWAYS_INLINE_ bool operator==(const Box<T, RefCounter> &p_r) const {
            return inner_ptr == p_r.inner_ptr;
        }
        _ALWAYS_INLINE_ bool operator!=(const Box<T, RefCounter> &p_r) const {
            return inner_ptr != p_r.inner_ptr;
        }
        _ALWAYS_INLINE_ T *operator->() {
            return &inner_ptr.ptr()->data;
        }
        _ALWAYS_INLINE_ T *operator*() {
            return &inner_ptr.ptr()->data;
        }
        _ALWAYS_INLINE_ const T *operator->() const {
            return &inner_ptr.ptr()->data;
        }
        _ALWAYS_INLINE_ const T *ptr() const {
            return &inner_ptr.ptr()->data;
        }
        _ALWAYS_INLINE_ T *ptr() {
            return &inner_ptr.ptr()->data;
        }
        _ALWAYS_INLINE_ const T& ref() const {
            return inner_ptr.ptr()->data;
        }
        _ALWAYS_INLINE_ T& ref() {
            return inner_ptr.ptr()->data;
        }
        _ALWAYS_INLINE_ const T *operator*() const {
            return &inner_ptr.ptr()->data;
        }
        _NO_DISCARD_ _ALWAYS_INLINE_ bool is_null() const { return inner_ptr.is_null(); }
        _NO_DISCARD_ _ALWAYS_INLINE_ bool is_valid() const { return inner_ptr.is_valid(); }
        _ALWAYS_INLINE_ void clear_reference() { inner_ptr = Ref<Box<T, RefCounter>::InnerPointer>::null(); }
        _ALWAYS_INLINE_ Box& operator=(const Box& p_other) {
            inner_ptr = p_other.inner_ptr;
            return *this;
        }
        Box() : inner_ptr() {}
        Box(const T& p_data) {
            inner_ptr = Ref<Box<T, RefCounter>::InnerPointer>::make_ref(p_data);
        }
        Box(const Box& p_other) {
            inner_ptr = p_other.inner_ptr;
        }
        template<class... Args >
        static Box<T, RefCounter> make_box(Args&&... args){
            Box<T, RefCounter> re{};
            re.inner_ptr = Ref<Box<T, RefCounter>::InnerPointer>::make_ref(args...);
            return re;
        }
        ~Box() = default;
    };
}

#endif //MICROJIT_HELPER_H

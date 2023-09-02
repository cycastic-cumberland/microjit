//
// Created by cycastic on 8/30/23.
//

#ifndef MICROJIT_EXPERIMENT_TYPE_H
#define MICROJIT_EXPERIMENT_TYPE_H

#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <cstdint>
#include <cstddef>
#include "def.h"

namespace microjit {
    class ObjectTools {
    public:
        template<class T>
        static void ctor(T* p_obj) { new (p_obj) T(); }
        template<class T>
        static void copy_ctor(T* p_obj, const T* p_copy_target) { new (p_obj) T(*p_copy_target); }
        template<class T>
        static void dtor(T* p_obj) { p_obj->~T(); }
        template<class T>
        static void assign(T* p_obj, const T* copy_target) { *p_obj = *copy_target; }
        template<typename From, typename To>
        static void convert(const From* p_from, To* p_to){ *p_to = To(*p_from); }
        template<typename From, typename To>
        static void coerce(const From* p_from, To* p_to){ *p_to = *(To*)p_from; }

        static void empty_ctor(void*) {  }
        static void empty_copy_ctor(void*, void*) {  }
        static void empty_dtor(void*) {  }

        template<typename T>
        static void add(T* p_re, const T* p_left, const T* p_right) { *p_re = *p_left + *p_right; }
        template<typename T>
        static void sub(T* p_re, const T* p_left, const T* p_right) { *p_re = *p_left - *p_right; }
        template<typename T>
        static void mul(T* p_re, const T* p_left, const T* p_right) { *p_re = *p_left * *p_right; }
        template<typename T>
        static void div(T* p_re, const T* p_left, const T* p_right) { *p_re = *p_left / *p_right; }
        template<typename T>
        static void mod(T* p_re, const T* p_left, const T* p_right) { *p_re = *p_left % *p_right; }
    };
    struct Type {
        const std::type_info *type_info;
        const size_t size;
        const void* copy_constructor;
        const void* destructor;
        const bool is_primitive;
    private:
        TYPE_CONSTEXPR Type(const std::type_info* p_info,
                            const size_t& p_size,
                            const void* p_cc,
                            const void* p_dtor,
                            const bool& p_fundamental)
                : type_info(p_info), size(p_size),
                  copy_constructor(p_cc), destructor(p_dtor), is_primitive(p_fundamental) {}

        static TYPE_CONSTEXPR Type create_internal(void (*)()){
            TYPE_CONSTEXPR auto copy_ctor = (const void*)ObjectTools::empty_copy_ctor;
            TYPE_CONSTEXPR auto dtor = (const void*)ObjectTools::empty_dtor;
            return { &typeid(void), 0, copy_ctor, dtor, true };
        }

        template<typename T>
        static TYPE_CONSTEXPR Type create_internal(T (*)()){
            TYPE_CONSTEXPR auto is_trivially_destructible  = std::is_trivially_destructible_v<T>;

            TYPE_CONSTEXPR auto copy_ctor = (const void*)ObjectTools::copy_ctor<T>;
            TYPE_CONSTEXPR auto dtor = (is_trivially_destructible ?
                                        (const void*)ObjectTools::empty_dtor :
                                        (const void*)ObjectTools::dtor<T>);
            TYPE_CONSTEXPR auto fundamental = std::is_fundamental_v<T>;
            return { &typeid(T), sizeof(T), copy_ctor, dtor, fundamental };
        }

    public:
        _NO_DISCARD_ _ALWAYS_INLINE_ TYPE_CONSTEXPR bool operator==(const Type& p_right) const {
            return type_info == p_right.type_info;
        }
        _NO_DISCARD_ _ALWAYS_INLINE_ TYPE_CONSTEXPR bool operator!=(const Type& p_right) const {
            return type_info != p_right.type_info;
        }

        template<typename T>
        static TYPE_CONSTEXPR Type create(){
            // A little cheat for void type
            TYPE_CONSTEXPR T (*f)() = nullptr;
            return create_internal(f);
        }
        static _ALWAYS_INLINE_ bool is_floating_point(Type p_type) {
            TYPE_CONSTEXPR auto float_type = Type::create<float>();
            TYPE_CONSTEXPR auto double_type = Type::create<double>();
            return (p_type == float_type || p_type == double_type);
        }
        static _ALWAYS_INLINE_ bool is_signed_integer(Type p_type) {
            TYPE_CONSTEXPR auto i8 = Type::create<int8_t>();
            TYPE_CONSTEXPR auto i16 = Type::create<int16_t>();
            TYPE_CONSTEXPR auto i32 = Type::create<int32_t>();
            TYPE_CONSTEXPR auto i64 = Type::create<int64_t>();
            return (p_type == i8) ||
                   (p_type == i16) ||
                   (p_type == i32) ||
                   (p_type == i64);
        }
    };
}

#endif //MICROJIT_EXPERIMENT_TYPE_H

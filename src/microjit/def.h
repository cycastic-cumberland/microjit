//
// Created by cycastic on 8/13/23.
//

#ifndef MICROJIT_DEF_H
#define MICROJIT_DEF_H

#if defined(__clang__) || defined(_MSC_VER)
#define USE_TYPE_CONSTEXPR
#define TYPE_CONSTEXPR constexpr
#define TYPE_ASSERT(m_expr) static_assert((m_expr))
#else
#define TYPE_CONSTEXPR
#define TYPE_ASSERT(m_expr) if (!(m_expr)) MJ_RAISE("Assertion failed")
#endif
#ifndef _ALWAYS_INLINE_
#if defined(__GNUC__)
#define _ALWAYS_INLINE_ __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define _ALWAYS_INLINE_ __forceinline
#else
#define _ALWAYS_INLINE_ inline
#endif
#endif

#ifndef _NO_DISCARD_
#define _NO_DISCARD_ [[nodiscard]]
#endif

#if !defined(likely) && !defined(unlikely)
#ifdef __GNUC__
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) x
#define unlikely(x) x
#endif
#endif

#ifndef SWAP
template <typename T>
void __swap(T& p_left, T& p_right){
    auto aux = p_left;
    p_left = p_right;
    p_right = aux;
}

#define SWAP(m_a, m_b) __swap((m_a), (m_b))
#endif

static _ALWAYS_INLINE_ constexpr size_t simple_16_bit_align(size_t p_num){
    return ((p_num + 15) / 16) * 16;
}

template <typename R, typename...Args>
static constexpr size_t calculate_args_space() {
    size_t ret_size = 0;
    if constexpr (!std::is_void_v<R>) ret_size = sizeof(R);
    if constexpr (sizeof...(Args))
        return simple_16_bit_align(ret_size + (sizeof(Args) + ...));
    else return simple_16_bit_align(ret_size);
}

#endif //MICROJIT_DEF_H

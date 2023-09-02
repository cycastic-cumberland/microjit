//
// Created by cycastic on 8/30/23.
//

#ifndef MICROJIT_EXPERIMENT_TRAMPOLINE_H
#define MICROJIT_EXPERIMENT_TRAMPOLINE_H

#include <functional>
#include "helper.h"
#include "type.h"

namespace microjit {
    template<typename F>
    class LambdaWrapper {
        struct Storage : public ThreadSafeObject {
            const F lambda;
            explicit Storage(const F& p_lambda) : lambda(p_lambda) {}
            ~Storage() override = default;
        };
        Ref<Storage> boxed_functor;
        const F* functor;
        static _ALWAYS_INLINE_ Ref<Storage> create_storage(const F& p_lambda){
            return Ref<Storage>::from_uninitialized_object(new Storage(p_lambda));
        }
    public:
        LambdaWrapper() : boxed_functor(), functor() {}
        explicit LambdaWrapper(const F& p_func)
            : boxed_functor(create_storage(p_func)), functor(&(boxed_functor->lambda)) {}
        LambdaWrapper(LambdaWrapper&& p_other) noexcept
            : boxed_functor(p_other.boxed_functor), functor(&(boxed_functor->lambda)) {
                p_other.boxed_functor = nullptr;
                p_other.functor = nullptr;
        }
        LambdaWrapper(const LambdaWrapper& p_other)
            : boxed_functor(p_other.boxed_functor), functor(&(boxed_functor->lambda)) {}

        LambdaWrapper& operator=(const LambdaWrapper& p_other) {
            boxed_functor = p_other.boxed_functor;
            functor = &(boxed_functor->lambda);
            return *this;
        }
        bool operator==(const LambdaWrapper& p_other) const {
            return boxed_functor == p_other.boxed_functor;
        }
        bool operator!=(const LambdaWrapper& p_other) const {
            return boxed_functor != p_other.boxed_functor;
        }
        bool operator==(std::nullptr_t) const {
            return boxed_functor.is_null();
        }
        bool operator!=(std::nullptr_t) const {
            return boxed_functor.is_valid();
        }

        template<typename T, typename...Args>
        void call(T* re, Args&&... args) const{
            *re = (*functor)(std::forward<Args>(args)...);
        }
        template<typename...Args>
        void call(Args&&... args) const{
            (*functor)(std::forward<Args>(args)...);
        }
    };
    template <typename R, typename...Args> class NativeFunctionTrampoline;
    class JitFunctionTrampoline;

    class BaseTrampoline : public ThreadUnsafeObject {
        template<typename T>
        static void create_args(std::vector<Type>* p_vec) {
            p_vec->push_back(Type::create<T>());
        }
    protected:
        void (*caller)(const BaseTrampoline*, uint8_t*) = nullptr;
    public:
        _NO_DISCARD_ _ALWAYS_INLINE_ decltype(caller) get_caller() const { return caller; }
        void call(uint8_t* p_stack) const {
            caller(this, p_stack);
        }

        template <typename R, typename...Args>
        static Ref<NativeFunctionTrampoline<R, Args...>> create_native_trampoline(R (*f)(Args...));
        template<class T>
        static Ref<JitFunctionTrampoline> create_jit_trampoline(const T* p_host, void (*p_recompile_cb)(const T*),
                                                 void (**p_actual_trampoline)(uint8_t *));
    };

    template <typename R, typename...Args>
    class NativeFunctionTrampoline : public BaseTrampoline {
    public:
        typedef R (*FunctorType)(Args...);
    private:
        FunctorType functor;
        friend class BaseTrampoline;
    public:
        static constexpr auto args_combined_size = (sizeof(Args) + ...);
        const Type return_type;
        const std::vector<Type> argument_types;
    private:
        template<typename T>
        static void create_args(std::vector<Type>* p_vec){
            p_vec->push_back(Type::create<T>());
        }
// Passed from left to right
#if defined(__clang__)
        template<typename T>
        T pass_arg(uint8_t **vrsp_ptr, int *argc,
                   uint8_t *old_rsp) const {
            auto new_offset = (size_t)*vrsp_ptr - sizeof(T);
            *vrsp_ptr = (uint8_t*)(new_offset);
            auto arg_ptr = (T*)(new_offset);
            if (*argc == argument_types.size() - 1){
                *vrsp_ptr = old_rsp;
            } else {
                *argc += 1;
            }
            return *arg_ptr;
        }
        template<typename ...Args2>
        void call_internal(void (*f)(Args2...), uint8_t* p_stack) const {
            constexpr auto args_space_size = calculate_args_space<R, Args...>();
            auto floor = p_stack;
            auto ceil = (uint8_t*)((size_t)p_stack + args_space_size);

            if constexpr (sizeof...(Args2) == 0) {
                f();
            } else {
                int i = 0;
                f(pass_arg<Args2>(&ceil, &i, floor)...);
            }
        }

        template<typename R2, typename ...Args2>
        void call_internal(R2 (*f)(Args2...), uint8_t* p_stack) const {
            constexpr auto args_space_size = calculate_args_space<R, Args...>();
            auto floor = p_stack;
            auto ceil = (uint8_t*)((size_t)p_stack + args_space_size);

            if constexpr (sizeof...(Args2) == 0) {
                f();
            } else {
                int i = 0;
                R2 re = f(pass_arg<Args2>(&ceil, &i, floor)...);
                if constexpr (std::is_fundamental_v<R2>) {
                    *(R2*)floor = re;
                } else {
                    auto cc = (void(*)(void*, const void*))return_type.copy_constructor;
                    cc(floor, &re);
                }
            }
        }
// Passed from right to left
#else
        template<typename T>
        T pass_arg(uint8_t **vrsp_ptr, int *argc,
                   uint8_t *old_rsp) const {
            auto arg_ptr = (T*)(*vrsp_ptr);
            if (*argc == argument_types.size() - 1){
                *vrsp_ptr = old_rsp;
            } else {
                *vrsp_ptr = (uint8_t*)((size_t)*vrsp_ptr + sizeof(T));
                *argc += 1;
            }
            return *arg_ptr;
        }
        template<typename ...Args2>
        void call_internal(void (*f)(Args2...), uint8_t* p_stack) const {
            constexpr auto args_space_size = calculate_args_space<R, Args...>();
            auto floor = p_stack;
            auto ceil = (uint8_t*)((size_t)floor + (args_space_size - args_combined_size));

            if constexpr (sizeof...(Args2) == 0) {
                f();
            } else {
                int i = 0;
                f(pass_arg<Args2>(&ceil, &i, floor)...);
            }
        }

        template<typename R2, typename ...Args2>
        void call_internal(R2 (*f)(Args2...), uint8_t* p_stack) const {
            constexpr auto args_space_size = calculate_args_space<R, Args...>();
            auto floor = p_stack;
            auto ceil = (uint8_t*)((size_t)floor + (args_space_size - args_combined_size));

            if constexpr (!sizeof...(Args2)) {
                f(pass_arg<Args>(p_stack, nullptr, 0, nullptr)...);
            } else {
                int i = 0;
                R2 re = f(pass_arg<Args2>(&ceil, &i, floor)...);
                if constexpr (std::is_fundamental_v<R2>) {
                    *(R2*)floor = re;
                } else {
                    auto cc = (void(*)(void*, const void*))return_type.copy_constructor;
                    cc(floor, &re);
                }
            }
        }
#endif
    public:
        _NO_DISCARD_ _ALWAYS_INLINE_ FunctorType get_functor() const {
            return functor;
        }
        static void call_final(const NativeFunctionTrampoline* p_self, uint8_t* p_stack) {
            p_self->call_internal(p_self->functor, p_stack);
        }
    private:
        NativeFunctionTrampoline(FunctorType f, Type p_ret_type, std::vector<Type>& p_arg_types)
                : functor(f), return_type(p_ret_type), argument_types(std::move(p_arg_types)) {
            caller = (decltype(caller))(call_final);
        }
    };

    class JitFunctionTrampoline : public BaseTrampoline {
    private:
        void (**actual_trampoline)(uint8_t *);
        void (*recompile_cb)(const void*);
        const void* host;
        friend class BaseTrampoline;
    public:
        static void call_final(const JitFunctionTrampoline* p_self, uint8_t* p_stack) {
            p_self->recompile_cb(p_self->host);
            (*(p_self->actual_trampoline))(p_stack);
        }
    private:
        JitFunctionTrampoline(const void* p_host, void (*p_recompile_cb)(const void*),
                              void (**p_actual_trampoline)(uint8_t*))
                : host(p_host), recompile_cb(p_recompile_cb),
                  actual_trampoline(p_actual_trampoline){
            caller = (decltype(caller))(call_final);
        }
    };
    template<typename R, typename... Args>
    Ref<NativeFunctionTrampoline<R, Args...>> BaseTrampoline::create_native_trampoline(R (*f)(Args...)) {
        std::vector<Type> arg_types{};
        arg_types.reserve(sizeof...(Args));
        Type ret_type = Type::create<R>();
        (create_args<Args>(&arg_types), ...);
        auto wrapped = new NativeFunctionTrampoline(f, ret_type, arg_types);
        return Ref<NativeFunctionTrampoline<R, Args...>>::from_uninitialized_object(wrapped);
    }

    template<class T>
    Ref<JitFunctionTrampoline> BaseTrampoline::create_jit_trampoline(const T *p_host, void (*p_recompile_cb)(const T *),
                                                                     void (**p_actual_trampoline)(uint8_t *)) {
        auto trampoline = new JitFunctionTrampoline((const void*)p_host,
                                                    (void (*)(const void*))p_recompile_cb,
                                                    p_actual_trampoline);
        return Ref<JitFunctionTrampoline>::from_uninitialized_object(trampoline);
    }
}

#endif //MICROJIT_EXPERIMENT_TRAMPOLINE_H

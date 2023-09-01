//
// Created by cycastic on 8/30/23.
//

#ifndef MICROJIT_EXPERIMENT_TRAMPOLINE_H
#define MICROJIT_EXPERIMENT_TRAMPOLINE_H

#include <functional>
#include "helper.h"
#include "virtual_stack.h"
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
    public:
        virtual void call(VirtualStack* p_stack) const = 0;

        template <typename R, typename...Args>
        static Ref<NativeFunctionTrampoline<R, Args...>> create_native_trampoline(R (*f)(Args...));
        template<class T>
        static Ref<JitFunctionTrampoline> create_jit_trampoline(const T* p_host, void (*p_recompile_cb)(const T*),
                                                 void (**p_actual_trampoline)(VirtualStack*));
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
        NativeFunctionTrampoline(FunctorType f, Type p_ret_type, std::vector<Type>& p_arg_types)
                : functor(f), return_type(p_ret_type), argument_types(std::move(p_arg_types)) {}
    private:
        template<typename T>
        static void create_args(std::vector<Type>* p_vec){
            p_vec->push_back(Type::create<T>());
        }
#if defined(__clang__)
        template<typename T>
        T pass_arg(VirtualStack* p_stack, VirtualStack::StackPtr *vrsp_ptr, int *argc,
                   VirtualStack::StackPtr old_rsp) const {
            *vrsp_ptr = (VirtualStack::StackPtr)((size_t)*vrsp_ptr - sizeof(T));
            auto arg_ptr = (T*)(*vrsp_ptr);
            if (*argc == argument_types.size() - 1){
                *vrsp_ptr = old_rsp;
            } else {
                *argc += 1;
            }
            return *arg_ptr;
        }
        template<typename ...Args2>
        void call_internal(void (*f)(Args2...), VirtualStack* p_stack) const {
            auto** vrsp_ptr = p_stack->rsp();
            auto** vrbp_ptr = p_stack->rbp();
            auto old_rsp = *vrsp_ptr;
            auto old_rbp = *vrbp_ptr;

            *vrbp_ptr = old_rsp;
            if constexpr (sizeof...(Args2) == 0) {
                f();
            } else {
                int i = 0;
                *vrsp_ptr = (VirtualStack::StackPtr)((size_t)old_rsp + args_combined_size);
                f(pass_arg<Args2>(p_stack, vrsp_ptr, &i, old_rsp)...);
            }

            *vrsp_ptr = old_rsp;
            *vrbp_ptr = old_rbp;
        }

        template<typename R2, typename ...Args2>
        void call_internal(R2 (*f)(Args2...), VirtualStack* p_stack) const {
            auto** vrsp_ptr = p_stack->rsp();
            auto** vrbp_ptr = p_stack->rbp();
            auto old_rsp = *vrsp_ptr;
            auto old_rbp = *vrbp_ptr;

            *vrbp_ptr = old_rsp;
            if constexpr (sizeof...(Args2) == 0) {
                f();
            } else {
                int i = 0;
                *vrsp_ptr = (VirtualStack::StackPtr)((size_t)old_rsp + args_combined_size);
                R2 re = f(pass_arg<Args2>(p_stack, vrsp_ptr, &i, old_rsp)...);
                if constexpr (std::is_fundamental_v<R2>) {
                    *(R2*)old_rsp = re;
                } else {
                    auto cc = (void(*)(void*, const void*))return_type.copy_constructor;
                    cc(old_rsp, &re);
                }
            }

            *vrsp_ptr = old_rsp;
            *vrbp_ptr = old_rbp;
        }
#else
        template<typename T>
        T pass_arg(VirtualStack* p_stack, VirtualStack::StackPtr *vrsp_ptr, int *argc,
                   VirtualStack::StackPtr old_rsp) const {
            auto arg_ptr = (T*)(*vrsp_ptr);
            if (*argc == argument_types.size() - 1){
                *vrsp_ptr = old_rsp;
            } else {
                *vrsp_ptr = (VirtualStack::StackPtr)((size_t)*vrsp_ptr + sizeof(T));
                *argc += 1;
            }
            return *arg_ptr;
        }
        template<typename ...Args2>
        void call_internal(void (*f)(Args2...), VirtualStack* p_stack) const {
            auto** vrsp_ptr = p_stack->rsp();
            auto** vrbp_ptr = p_stack->rbp();
            auto old_rsp = *vrsp_ptr;
            auto old_rbp = *vrbp_ptr;

            *vrbp_ptr = old_rsp;
            if constexpr (sizeof...(Args2) == 0) {
                f();
            } else {
                int i = 0;
                f(pass_arg<Args>(p_stack, vrsp_ptr, &i, old_rsp)...);
            }

            *vrsp_ptr = old_rsp;
            *vrbp_ptr = old_rbp;
        }

        template<typename R2, typename ...Args2>
        void call_internal(R2 (*f)(Args2...), VirtualStack* p_stack) const {
            auto** vrsp_ptr = p_stack->rsp();
            auto** vrbp_ptr = p_stack->rbp();
            auto old_rsp = *vrsp_ptr;
            auto old_rbp = *vrbp_ptr;

            *vrbp_ptr = old_rsp;
            if (argument_types.empty()) {
                f(pass_arg<Args>(p_stack, nullptr, 0, nullptr)...);
            } else {
                int i = 0;
                R2 re = f(pass_arg<Args>(p_stack, vrsp_ptr, &i, old_rsp)...);
                if constexpr (std::is_fundamental_v<R2>) {
                    *(R2*)old_rsp = re;
                } else {
                    auto cc = (void(*)(void*, const void*))return_type.copy_constructor;
                    cc(old_rsp, &re);
                }
            }

            *vrsp_ptr = old_rsp;
            *vrbp_ptr = old_rbp;
        }
#endif
    public:
        _NO_DISCARD_ _ALWAYS_INLINE_ FunctorType get_functor() const {
            return functor;
        }
        void call(VirtualStack* p_stack) const override {
            call_internal(functor, p_stack);
        }
    };

    class JitFunctionTrampoline : public BaseTrampoline {
    private:
        void (**actual_trampoline)(VirtualStack*);
        void (*recompile_cb)(const void*);
        const void* host;
        friend class BaseTrampoline;

        JitFunctionTrampoline(const void* p_host, void (*p_recompile_cb)(const void*),
                              void (**p_actual_trampoline)(VirtualStack*))
                : host(p_host), recompile_cb(p_recompile_cb),
                  actual_trampoline(p_actual_trampoline){}
    public:
        void call_final(VirtualStack* p_stack) const {
            auto** vrsp_ptr = p_stack->rsp();
            auto** vrbp_ptr = p_stack->rbp();
            auto old_rsp = *vrsp_ptr;
            auto old_rbp = *vrbp_ptr;
            *vrbp_ptr = old_rsp;
            recompile_cb(host);
            (*actual_trampoline)(p_stack);
            *vrsp_ptr = old_rsp;
            *vrbp_ptr = old_rbp;
        }
        void call(VirtualStack* p_stack) const override {
            call_final(p_stack);
        }
    };

    // template<typename R, typename ...Args>
    // class InstanceTrampoline : public BaseTrampoline {
    // private:
    //     void (**actual_trampoline)(VirtualStack*);
    //     void (*recompile_cb)(const void*);
    //     const void* host;
    //     friend class BaseTrampoline;

    //     InstanceTrampoline(const void* p_host, void (*p_recompile_cb)(const void*),
    //                           void (**p_actual_trampoline)(VirtualStack*))
    //             : host(p_host), recompile_cb(p_recompile_cb),
    //               actual_trampoline(p_actual_trampoline){}

    //     template<typename T>
    //     static _ALWAYS_INLINE_ void move_argument(const T &p_arg, VirtualStack *p_stack){
    //         static constexpr bool trivially_copy_constructable = std::is_trivially_copy_constructible_v<T>;
    //         auto stack_ptr = p_stack->rsp();
    //         *stack_ptr = (VirtualStack::StackPtr)((size_t)(*stack_ptr) - sizeof(T));
    //         if constexpr (trivially_copy_constructable){
    //             *(T*)(*stack_ptr) = p_arg;
    //         } else
    //             new (*stack_ptr) T(p_arg);
    //     }
    //     template<typename T>
    //     static _ALWAYS_INLINE_ void destruct_argument(const T &, VirtualStack *p_stack){
    //         static constexpr bool trivially_destructible = std::is_trivially_destructible_v<T>;
    //         auto stack_ptr = p_stack->rsp();
    //         auto ptr = *stack_ptr;
    //         if constexpr (!trivially_destructible)
    //             ((T*)ptr)->~T();
    //         *stack_ptr = (VirtualStack::StackPtr)((size_t)ptr + sizeof(T));
    //     }

    //     template<typename T>
    //     static _ALWAYS_INLINE_ void destruct_return(VirtualStack *p_stack){
    //         static constexpr bool trivially_destructible = std::is_trivially_destructible_v<T>;
    //         auto stack_ptr = p_stack->rsp();
    //         auto ptr = *stack_ptr;
    //         if constexpr (!trivially_destructible)
    //             ((T*)ptr)->~T();
    //         *stack_ptr = (VirtualStack::StackPtr)((size_t)ptr + sizeof(T));
    //     }

    //     template<typename ...Args2>
    //     static void call_internal(const InstanceTrampoline<void, Args2...>* p_self, VirtualStack* p_stack, Args2&&... args);
    //     template<typename R2, typename ...Args2>
    //     static R2 call_internal(const InstanceTrampoline<R2, Args2...>* p_self, VirtualStack* p_stack, Args2&&... args);
    // public:
    //     R call_final(VirtualStack* p_stack, Args&&... args) const {
    //         return call_internal(this, std::forward<Args>(args)...);
    //     }
    //     void call(VirtualStack* p_stack) const override {  }
    // };

    // template<typename R, typename... Args>
    // template<typename... Args2>
    // void InstanceTrampoline<R, Args...>::call_internal(const InstanceTrampoline<void, Args2...> *p_self,
    //                                                    VirtualStack* p_stack, Args2&&... args) {
    //     auto self = ((InstanceTrampoline<R, Args...>*)p_self);
    //     self->recompile_cb(self->host);
    //     auto old_rbp = *p_stack->rbp();
    //     auto old_rsp = *p_stack->rsp();
    //     (move_argument<Args2>(args, p_stack), ...);
    //     auto args_end = *p_stack->rsp();
    //     *p_stack->rbp() = *p_stack->rsp();
    //     (*self->actual_trampoline)(p_stack);
    //     if (sizeof...(Args)){
    //         *p_stack->rsp() = args_end;
    //         (destruct_argument<Args2>(args, p_stack), ...);
    //     }
    //     *p_stack->rbp() = old_rbp;
    //     *p_stack->rsp() = old_rsp;
    // }

    // template<typename R, typename... Args>
    // template<typename R2, typename... Args2>
    // R2 InstanceTrampoline<R, Args...>::call_internal(const InstanceTrampoline<R2, Args2...> *p_self,
    //                                                    VirtualStack* p_stack, Args2&&... args) {
    //     auto self = ((InstanceTrampoline<R, Args...>*)p_self);
    //     self->recompile_cb(self->host);
    //     auto old_rbp = *p_stack->rbp();
    //     auto old_rsp = *p_stack->rsp();
    //     (move_argument<Args2>(args, p_stack), ...);
    //     const auto return_slot = (VirtualStack::StackPtr)((size_t)*p_stack->rsp() - sizeof(R2));
    //     (*p_stack->rbp()) = return_slot;
    //     (*p_stack->rsp()) = return_slot;
    //     (*self->actual_trampoline)(p_stack);
    //     auto re = *(R2*)return_slot;
    //     // After copying the return value, destroy its stack entry
    //     destruct_return<R2>(p_stack);
    //     if (sizeof...(Args)){
    //         (destruct_argument<Args2>(args, p_stack), ...);
    //     }
    //     *p_stack->rbp() = old_rbp;
    //     *p_stack->rsp() = old_rsp;
    //     return re;
    // }

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
                                                                     void (**p_actual_trampoline)(VirtualStack *)) {
        auto trampoline = new JitFunctionTrampoline((const void*)p_host,
                                                    (void (*)(const void*))p_recompile_cb,
                                                    p_actual_trampoline);
        return Ref<JitFunctionTrampoline>::from_uninitialized_object(trampoline);
    }

    // template<class T, typename R, typename... Args>
    // Ref<InstanceTrampoline<R, Args...>>
    // BaseTrampoline::create_orchestrator_instance_trampoline(const T *p_host, void (*p_recompile_cb)(const T *),
    //                                                         void (**p_actual_trampoline)(VirtualStack *)) {
    //     auto trampoline = new InstanceTrampoline<R, Args...>((const void*)p_host,
    //                                                          (void (*)(const void*))p_recompile_cb,
    //                                                          p_actual_trampoline);
    //     return Ref<InstanceTrampoline<R, Args...>>::from_uninitialized_object(trampoline);
    // }
}

#endif //MICROJIT_EXPERIMENT_TRAMPOLINE_H

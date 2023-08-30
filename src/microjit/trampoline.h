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
    template <typename F, typename...Args> class NativeFunctionTrampoline;

    class WrapperUtility {
    private:
        template<typename F>
        static LambdaWrapper<F> create_wrapper(const F& p_lambda) {
            return LambdaWrapper(p_lambda);
        }
    };


    class BaseTrampoline : public ThreadUnsafeObject {
        template<typename T>
        static void create_args(std::vector<Type>* p_vec) {
            p_vec->push_back(Type::create<T>());
        }
    public:
        virtual void call(VirtualStack* p_stack) const = 0;

        template <typename R, typename...Args>
        static Ref<NativeFunctionTrampoline<R, Args...>> create_native_function(R (*f)(Args...));
    };

    template <typename R, typename...Args>
    class NativeFunctionTrampoline : public BaseTrampoline {
    public:
        typedef R (*FunctorType)(Args...);
    private:
        FunctorType functor;
    public:
        const Type return_type;
        const std::vector<Type> argument_types;
    private:
        template<typename T>
        static void create_args(std::vector<Type>* p_vec){
            p_vec->push_back(Type::create<T>());
        }
        NativeFunctionTrampoline(FunctorType f, Type p_ret_type, std::vector<Type>& p_arg_types)
            : functor(f), return_type(p_ret_type), argument_types(std::move(p_arg_types)) {}
        friend class BaseTrampoline;
        template<typename T>
        T pass_arg(VirtualStack* p_stack, VirtualStack::StackPtr *vrsp_ptr, int *argc,
                   VirtualStack::StackPtr old_rsp) const {
            auto arg_ptr = (T*)(*vrsp_ptr);
            if (*argc == argument_types.size() - 1){
                *vrsp_ptr = old_rsp;
            } else {
                *vrsp_ptr = (VirtualStack::StackPtr)((size_t)*vrsp_ptr + sizeof(T));
            }
            *argc += 1;
            return *arg_ptr;
        }

        template<typename ...Args2>
        void call_internal(void (*f)(Args...), VirtualStack* p_stack) const {
            auto** vrsp_ptr = p_stack->rsp();
            auto** vrbp_ptr = p_stack->rbp();
            auto old_rsp = *vrsp_ptr;
            auto old_rbp = *vrbp_ptr;

            *vrbp_ptr = old_rsp;
            if (argument_types.empty()) {
                f(pass_arg<Args>(p_stack, nullptr, 0, nullptr)...);
            } else {
                int i = 0;
                f(pass_arg<Args>(p_stack, vrsp_ptr, &i, old_rsp)...);
            }

            *vrsp_ptr = old_rsp;
            *vrbp_ptr = old_rbp;
        }

        template<typename R2, typename ...Args2>
        void call_internal(R2 (*f)(Args...), VirtualStack* p_stack) const {
            auto** vrsp_ptr = p_stack->rsp();
            auto** vrbp_ptr = p_stack->rbp();
            auto old_rsp = *vrsp_ptr;
            auto old_rbp = *vrbp_ptr;

            *vrbp_ptr = old_rsp;
            if (argument_types.empty()) {
                f(pass_arg<Args>(p_stack, nullptr, 0, nullptr)...);
            } else {
                int i = 0;
                *vrsp_ptr = (VirtualStack::StackPtr)(size_t)(old_rsp + return_type.size);
                R2 re = f(pass_arg<Args>(p_stack, vrsp_ptr, &i, old_rsp)...);
                auto cc = (void(*)(void*, const void*))return_type.copy_constructor;
                cc(old_rsp, &re);
            }

            *vrsp_ptr = old_rsp;
            *vrbp_ptr = old_rbp;
        }
    public:
        _NO_DISCARD_ _ALWAYS_INLINE_ FunctorType get_functor() const {
            return functor;
        }
        void call(VirtualStack* p_stack) const override {
            call_internal(functor, p_stack);
        }
    };

    template<typename R, typename... Args>
    Ref<NativeFunctionTrampoline<R, Args...>> BaseTrampoline::create_native_function(R (*f)(Args...)) {
        std::vector<Type> arg_types{};
        arg_types.reserve(sizeof...(Args));
        Type ret_type = Type::create<R>();
        (create_args<Args>(&arg_types), ...);
        auto wrapped = new NativeFunctionTrampoline(f, ret_type, arg_types);
        return Ref<NativeFunctionTrampoline<R, Args...>>::from_uninitialized_object(wrapped);
    }
    class JitFunctionTrampoline : public BaseTrampoline {
    private:
        void (**actual_trampoline)(VirtualStack*);
        void (*recompile_cb)(const void*);
        const void* host;
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
        template<class T>
        static Ref<JitFunctionTrampoline> create(const T* p_host, void (*p_recompile_cb)(const T*),
                                                 void (**p_actual_trampoline)(VirtualStack*)){
            auto trampoline = new JitFunctionTrampoline((const void*)p_host,
                                                        (void (*)(const void*))p_recompile_cb,
                                                        p_actual_trampoline);
            return Ref<JitFunctionTrampoline>::from_uninitialized_object(trampoline);
        }
    };
}

#endif //MICROJIT_EXPERIMENT_TRAMPOLINE_H

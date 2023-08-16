//
// Created by cycastic on 8/15/23.
//

#ifndef MICROJIT_ORCHESTRATOR_H
#define MICROJIT_ORCHESTRATOR_H

#include "instructions.h"
#include "utils.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "jit_x86_64.h"
#endif

namespace microjit {
    template<class CompilerTy>
    class OrchestratorComponent : public ThreadUnsafeObject {
    public:
        typedef void(*VirtualStackFunction)(VirtualStack*);

        template<typename R, typename ...Args>
        struct Instance : public ThreadUnsafeObject {
        private:
            typedef OrchestratorComponent<CompilerTy> Host;
        private:
            const OrchestratorComponent* parent;
            const Ref<Function<R, Args...>> function;
            mutable std::function<R(VirtualStack*, Args...)> compiled_function{};
        private:
            template<typename R2, typename ...Args2>
            void compile_internal(std::function<R2(VirtualStack*, Args2...)>* p_func) const;
            template<typename ...Args2>
            void compile_internal(std::function<void(VirtualStack*, Args2...)>* p_func) const;
            std::function<R(VirtualStack*)> get_bound_function(Args&&... args) const;
        public:
            explicit Instance(const OrchestratorComponent* p_orchestrator)
                : parent(p_orchestrator), function{Ref<Function<R, Args...>>::make_ref()} {}
            Ref<Function<R, Args...>> get_function() const { return function; }
            R call(Args&&... args) const;
            R call_with_vstack(VirtualStack* p_stack, Args&&... args) const;
            void recompile() const;
            ~Instance() override = default;
        };
        template<typename R, typename ...Args>
        struct InstanceWrapper {
        private:
            Ref<Instance<R, Args...>> instance;
        public:
            explicit InstanceWrapper(const Ref<Instance<R, Args...>>& p_instance) : instance(p_instance) {}

            _NO_DISCARD_ Instance<R, Args...>* ptr() { return instance.ptr(); }
            _NO_DISCARD_ const Instance<R, Args...>* ptr() const { return instance.ptr(); }
            _NO_DISCARD_ Instance<R, Args...>* operator->() { return ptr(); }
            _NO_DISCARD_ const Instance<R, Args...>* operator->() const { return ptr(); }
            _NO_DISCARD_ Instance<R, Args...>* operator*() { return ptr(); }
            _NO_DISCARD_ const Instance<R, Args...>* operator*() const { return ptr(); }

            R operator()(Args&&... args) const {
                return instance->call(args...);
            }
        };
        struct InstanceHub {
        private:
            OrchestratorComponent* parent;
        public:
            explicit InstanceHub(OrchestratorComponent* p_orchestrator) : parent(p_orchestrator) {}
            _NO_DISCARD_ VirtualStackFunction fetch_function(const Ref<RectifiedFunction> &p_func) const;
        };
    private:
        friend struct InstanceHub;

        const InstanceHub hub;
        Ref<CompilerTy> compiler{};
        Ref<MicroJITRuntime> runtime{};

        std::unordered_map<size_t, VirtualStackFunction> function_map{};
        std::unordered_map<size_t, Ref<ThreadUnsafeObject>> instance_map{};
    private:
        MicroJITCompiler::CompilationResult compile(const Ref<RectifiedFunction>& p_func) {
            return compiler->compile(p_func);
        }
        void add_function(VirtualStackFunction p_cb, const Ref<RectifiedFunction> &p_from){
            function_map[(size_t)p_from->host] = p_cb;
        }
        bool has_function(const Ref<RectifiedFunction> &p_func) const {
            return function_map.find((size_t)p_func->host) != function_map.end();
        }
        VirtualStackFunction fetch_function(const Ref<RectifiedFunction> &p_func) {
            try {
                return function_map.at((size_t)p_func->host);
            } catch (const std::out_of_range&){
                auto compilation_result = compile(p_func);
                if (compilation_result.error) throw MicroJITException(std::string(__FILE__) +
                    " at (" + __FUNCTION__  + ":" + std::to_string(__LINE__) + "): " + ("Compilation failed"));
                const auto cb = (VirtualStackFunction)compilation_result.assembly->callback;
                add_function(cb, p_func);
                return cb;
            }
        }
        template<typename R, typename ...Args>
        InstanceWrapper<R, Args...> create_instance_internal() {
            auto instance = Ref<Instance<R, Args...>>::make_ref(this);
            instance_map[(size_t)(instance.ptr())] = instance.template c_style_cast<ThreadUnsafeObject>();
            return InstanceWrapper<R, Args...>(instance);
        }
    public:
        OrchestratorComponent()
            : hub(this) {
            runtime = Ref<MicroJITRuntime>::make_ref();
            compiler = Ref<CompilerTy>::make_ref(runtime);
        }
        template<typename R, typename ...Args>
        InstanceWrapper<R, Args...> create_instance() {
            return create_instance_internal<R, Args...>();
        }
        template<typename R, typename ...Args>
        InstanceWrapper<R, Args...> create_instance_from_model(const std::function<R(Args...)>&) {
            return create_instance_internal<R, Args...>();
        }
    };

    template<class CompilerTy>
    template<typename R, typename... Args>
    void OrchestratorComponent<CompilerTy>::Instance<R, Args...>::recompile() const {
        std::function<R(VirtualStack*, Args...)> dummy{};
        compiled_function.swap(dummy);
        compile_internal(&compiled_function);
    }

    template<class CompilerTy>
    template<typename R, typename... Args>
    std::function<R(VirtualStack *)>
    OrchestratorComponent<CompilerTy>::Instance<R, Args...>::get_bound_function(Args &&... args) const {
        using namespace std::placeholders;
        compile_internal(&compiled_function);
        std::function<decltype(R(args...))(VirtualStack*)> bound_function = std::bind(std::forward<decltype(compiled_function)>(compiled_function),
                                                                                      _1,
                                                                                      std::forward<Args>(args)...);
        return bound_function;
    }

    template<class CompilerTy>
    template<typename R, typename... Args>
    R OrchestratorComponent<CompilerTy>::Instance<R, Args...>::call_with_vstack(VirtualStack *p_stack,
                                                                                Args &&... args) const {
        return get_bound_function(args...)(p_stack);
    }

    template<class CompilerTy>
    template<typename R, typename... Args>
    R OrchestratorComponent<CompilerTy>::Instance<R, Args...>::call(Args &&... args) const {
        Box<VirtualStack> new_stack = Box<VirtualStack>::make_box(1024 * 1024 * 8, 128);
        return call_with_vstack(new_stack.ptr(), args...);
    }

    template<typename T>
    static _ALWAYS_INLINE_ void move_argument(const T &p_arg, VirtualStack *p_stack){
        auto stack_ptr = p_stack->rsp();
        *stack_ptr = (VirtualStack::StackPtr)((size_t)(*stack_ptr) - sizeof(T));
        new (*stack_ptr) T(p_arg);
    }

    template<class CompilerTy>
    template<typename R, typename... Args>
    template<typename R2, typename... Args2>
    void OrchestratorComponent<CompilerTy>::Instance<R, Args...>::compile_internal(
            std::function<R2(VirtualStack *, Args2...)> *p_func) const {
        static const auto hub_offset = ((size_t)(&(((Host*)0)->hub)));

        if (compiled_function) return;
        const auto& instance_hub = *(InstanceHub*)(&((uint8_t *)parent)[hub_offset]);
        auto cb = instance_hub.fetch_function(function->rectify());
        std::function<R2(VirtualStack*, Args2...)> new_func = [cb](VirtualStack* p_stack, Args2&& ...args) -> R2 {
            auto old_rbp = *p_stack->rbp();
            auto old_rsp = *p_stack->rsp();
            (move_argument<Args2>(args, p_stack), ...);
            const auto return_slot = (VirtualStack::StackPtr)((size_t)(*p_stack->rsp()) - sizeof(R2));
            (*p_stack->rbp()) = return_slot;
            (*p_stack->rsp()) = return_slot;
            cb(p_stack);
            auto re = (R2*)return_slot;
            *p_stack->rbp() = old_rbp;
            *p_stack->rsp() = old_rsp;
            return *re;
        };
        p_func->swap(new_func);
    }

    template<class CompilerTy>
    template<typename R, typename... Args>
    template<typename... Args2>
    void OrchestratorComponent<CompilerTy>::Instance<R, Args...>::compile_internal(
            std::function<void(VirtualStack *, Args2...)> *p_func) const {
        static const auto hub_offset = ((size_t)(&(((Host*)0)->hub)));

        if (compiled_function) return;
        const auto& instance_hub = *(InstanceHub*)(&((uint8_t *)parent)[hub_offset]);
        auto cb = instance_hub.fetch_function(function->rectify());
        std::function<void(VirtualStack*, Args2...)> new_func = [cb](VirtualStack* p_stack, Args2&& ...args) -> void {
            auto old_rbp = *p_stack->rbp();
            auto old_rsp = *p_stack->rsp();
            (move_argument<Args2>(args, p_stack), ...);
            *p_stack->rbp() = *p_stack->rsp();
            cb(p_stack);
            *p_stack->rbp() = old_rbp;
            *p_stack->rsp() = old_rsp;
        };
        p_func->swap(new_func);
    }


    template<class CompilerTy>
    typename OrchestratorComponent<CompilerTy>::VirtualStackFunction
    OrchestratorComponent<CompilerTy>::InstanceHub::fetch_function(const Ref<RectifiedFunction> &p_func) const {
        return parent->fetch_function(p_func);
    }

}

#if defined(__x86_64__) || defined(_M_X64)
    typedef microjit::OrchestratorComponent<microjit::MicroJITCompiler_x86_64> MicroJITOrchestrator;
#endif
#endif //MICROJIT_ORCHESTRATOR_H

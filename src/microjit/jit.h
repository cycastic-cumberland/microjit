//
// Created by cycastic on 8/15/23.
//

#ifndef MICROJIT_JIT_CORE_H
#define MICROJIT_JIT_CORE_H

#include <asmjit/asmjit.h>
#include "instructions.h"

namespace microjit {
    class MicroJITRuntime : public ThreadUnsafeObject {
    private:
        asmjit::JitRuntime runtime{};
    public:
        asmjit::JitRuntime& get_asmjit_runtime() { return runtime; }
    };
    class MicroJITCompiler : public ThreadUnsafeObject {
    public:
        struct Assembly : public ThreadUnsafeObject {
            asmjit::CodeHolder code{};
            Box<asmjit::x86::Assembler> assembler{};
            void* callback{};
            explicit Assembly(const asmjit::JitRuntime& p_runtime){
                code.init(p_runtime.environment());
                assembler = Box<asmjit::x86::Assembler>::make_box(&code);
            }
        };
        struct CompilationResult {
            uint32_t error;
            Ref<Assembly> assembly;
        };
    protected:
        Ref<MicroJITRuntime> runtime;
        virtual CompilationResult compile_internal(const Ref<RectifiedFunction>& p_func) { return {}; }
    public:
        static void raise_stack_overflown(){
            static constexpr auto message = "MicroJIT instance: Stack overflown\n";
            static constexpr int* the_funny = nullptr;
            fprintf(stderr, message);
            int a = *the_funny;
        }

        explicit MicroJITCompiler(const Ref<MicroJITRuntime>& p_runtime) : runtime(p_runtime) {}
        CompilationResult compile(const Ref<RectifiedFunction>& p_func) {
            return compile_internal(p_func);
        }
    };
}

#endif //MICROJIT_JIT_CORE_H

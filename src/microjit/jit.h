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
        template<class T>
        struct InstructionHasher {
            size_t operator()(const Ref<T>& p_ins) const{
                // Just get use the pointer as a hash lol
                return size_t(p_ins.ptr());
            }
        };
        struct StackFrameInfo : public ThreadUnsafeObject {
        public:
            size_t max_frame_size{};
            uint32_t max_object_allocation{};
            std::unordered_map<Ref<VariableInstruction>, int64_t, InstructionHasher<VariableInstruction>> variable_map{};
        };
    protected:
        Ref<MicroJITRuntime> runtime;
        virtual CompilationResult compile_internal(const Ref<RectifiedFunction>& p_func) { return {}; }
        static Ref<StackFrameInfo> create_frame_report(const Ref<RectifiedFunction>& p_func);
    public:
        static void raise_stack_overflown(){
            static constexpr char message[36] = "MicroJIT instance: Stack overflown\n";
            static constexpr int* the_funny = nullptr;
            fprintf(stderr, message);
            int a = *the_funny;
            fprintf(stdout, "Well what now...");
        }

        explicit MicroJITCompiler(const Ref<MicroJITRuntime>& p_runtime) : runtime(p_runtime) {}
        CompilationResult compile(const Ref<RectifiedFunction>& p_func) {
            return compile_internal(p_func);
        }
    };
}

#endif //MICROJIT_JIT_CORE_H

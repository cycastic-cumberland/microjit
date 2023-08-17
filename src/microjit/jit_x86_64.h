//
// Created by cycastic on 8/15/23.
//

#ifndef MICROJIT_JIT_X86_64_H
#define MICROJIT_JIT_X86_64_H
#if defined(__x86_64__) || defined(_M_X64)

#if !defined(__linux__)
#warning This library has only been tested on x86_64 linux, use other build target at your own discretion
#endif

#include "jit.h"

namespace microjit {
    class MicroJITCompiler_x86_64 : public MicroJITCompiler {
    private:
        static void copy_immediate_primitive(microjit::Box<asmjit::x86::Assembler> &assembler,
                                             const microjit::Ref<microjit::CopyConstructInstruction> &p_instruction);
        static void copy_immediate(microjit::Box<asmjit::x86::Assembler> &assembler,
                                   const microjit::Ref<microjit::CopyConstructInstruction> &p_instruction);
        static void copy_argument(microjit::Box<asmjit::x86::Assembler> &assembler,
                                  const microjit::Ref<microjit::CopyConstructInstruction> &p_instruction,
                                  const int64_t& p_offset,
                                  const uint32_t& p_arg_offset);
        static void call_destructors(Box<asmjit::x86::Assembler>& assembler,
                                     const std::unordered_map<size_t, int64_t>& p_offset_map,
                                     const Ref<RectifiedScope>& p_scope);
    protected:
        CompilationResult compile_internal(const Ref<RectifiedFunction>& p_func) override;
    public:
        explicit MicroJITCompiler_x86_64(const Ref<MicroJITRuntime>& p_runtime) : MicroJITCompiler(p_runtime) {}
    };
}
#endif

#endif //MICROJIT_JIT_X86_64_H

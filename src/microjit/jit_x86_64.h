//
// Created by cycastic on 8/15/23.
//

#ifndef MICROJIT_JIT_X86_64_H
#define MICROJIT_JIT_X86_64_H
#if defined(__x86_64__) || defined(_M_X64)

#if !defined(__linux__)
#warning This library has only been tested on x86_64 linux, use other build target at your own discretion
#endif

#include <stack>
#include "jit.h"
#include "x86_64_primitive_converter.gen.h"

//#define VERBOSE_ASSEMBLER_LOG

#if defined(DEBUG_ENABLED) && defined(VERBOSE_ASSEMBLER_LOG) && !defined(AIN)
#include <iostream>
#define AINL(m_message) std::cout << "[ASM: " << (__LINE__) << "]\t\t" << m_message << "\n"
#define AIN(m_action) std::cout << "[ASM: " << (__LINE__) << "]\t" << &(#m_action)[11] << "\n"; (m_action)
#else
#define AINL(m_message) ((void)0)
#define AIN(m_action) (m_action)
#endif

namespace microjit {
    class MicroJITCompiler_x86_64 : public MicroJITCompiler {
    private:
        struct ScopeInfo {
            Ref<RectifiedScope> scope;
            int64_t iterating;
            Box<asmjit::Label> label;
        };
        x86_64PrimitiveConverter converter{};
    private:
        template<class T>
        static void copy_immediate_primitive(microjit::Box<asmjit::x86::Assembler> &assembler,
                                             const microjit::Ref<T> &p_instruction);
        template<class T>
        static void copy_immediate(microjit::Box<asmjit::x86::Assembler> &assembler,
                                   const microjit::Ref<T> &p_instruction);
        static void copy_variable(microjit::Box<asmjit::x86::Assembler> &assembler,
                                  const microjit::Ref<microjit::CopyConstructInstruction> &p_instruction,
                                  const int64_t& p_offset,
                                  const int64_t& p_copy_target);
        static void assign_variable(microjit::Box<asmjit::x86::Assembler> &assembler,
                                    const microjit::Ref<microjit::AssignInstruction> &p_instruction,
                                    const int64_t& p_offset,
                                    const int64_t& p_copy_target);
        static void iterative_destructor_call(microjit::Box<asmjit::x86::Assembler> &assembler,
                                              const Ref<StackFrameInfo>& p_frame_info,
                                              std::stack<ScopeInfo>& p_scope_stack);

    protected:
        CompilationResult compile_internal(const Ref<RectifiedFunction>& p_func) override;
    public:
        explicit MicroJITCompiler_x86_64(const Ref<MicroJITRuntime>& p_runtime) : MicroJITCompiler(p_runtime) {}
    };
}


template<class T>
void microjit::MicroJITCompiler_x86_64::copy_immediate_primitive(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                                 const microjit::Ref<T> &p_instruction) {
    const auto& type_data = p_instruction->target_variable->type;
    auto imm = p_instruction->value_reference.template c_style_cast<ImmediateValue>();
    auto as_byte_array = imm->data;
    switch (type_data.size) {
        case 1:
            AIN(assembler->mov(asmjit::x86::cl, *(const uint8_t*)as_byte_array));
            AIN(assembler->mov(asmjit::x86::byte_ptr(asmjit::x86::rdi), asmjit::x86::cl));
            break;
        case 2:
            AIN(assembler->mov(asmjit::x86::cx, *(const uint16_t*)as_byte_array));
            AIN(assembler->mov(asmjit::x86::word_ptr(asmjit::x86::rdi), asmjit::x86::cx));
            break;
        case 4:
            AIN(assembler->mov(asmjit::x86::ecx, *(const uint32_t*)as_byte_array));
            AIN(assembler->mov(asmjit::x86::dword_ptr(asmjit::x86::rdi), asmjit::x86::ecx));
            break;
        case 8:
            AIN(assembler->mov(asmjit::x86::rcx, *(const uint64_t*)as_byte_array));
            AIN(assembler->mov(asmjit::x86::qword_ptr(asmjit::x86::rdi), asmjit::x86::rcx));
            break;
    }
}

template<class T>
void microjit::MicroJITCompiler_x86_64::copy_immediate(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                       const microjit::Ref<T> &p_instruction) {
    const auto& type_data = p_instruction->target_variable->type;
    auto imm = p_instruction->value_reference.template c_style_cast<ImmediateValue>();
    // The value to be copied, as byte array
    auto as_byte_array = imm->data;
    // Copy destination is currently in rdi
    // Allocate immediate value space on real stack
    // Copy the value onto real stack, byte by byte
    AIN(assembler->sub(asmjit::x86::rsp, type_data.size));
    // Copying by receding chunks did not work, not sure why
    // Copying bytes-by-bytes would have to do it for now
    switch (type_data.size) {
        case 1:{
            AIN(assembler->mov(asmjit::x86::byte_ptr(asmjit::x86::rsp), *(const uint8_t*)as_byte_array));
            break;
        }
        case 2:{
            AIN(assembler->mov(asmjit::x86::word_ptr(asmjit::x86::rsp), *(const uint16_t*)as_byte_array));
            break;
        }
        case 4:{
            AIN(assembler->mov(asmjit::x86::dword_ptr(asmjit::x86::rsp), *(const uint32_t*)as_byte_array));
            break;
        }
        case 8:{
            AIN(assembler->mov(asmjit::x86::rcx, *(const uint64_t*)as_byte_array));
            AIN(assembler->mov(asmjit::x86::qword_ptr(asmjit::x86::rsp), asmjit::x86::rcx));
            break;
        }
        default:{
            for (auto seek = decltype(type_data.size)(); seek < type_data.size; seek++) {
                AIN(assembler->mov(asmjit::x86::byte_ptr(asmjit::x86::rsp, seek), ((const uint8_t*)as_byte_array)[seek]));
            }
        }
    }
    // Move copy target (which is going to be real rsp) to rsi
    AIN(assembler->mov(asmjit::x86::rsi, asmjit::x86::rsp));
    // Call copy constructor
    AIN(assembler->call(p_instruction->ctor));
    // Return the stack space
    AIN(assembler->add(asmjit::x86::rsp, type_data.size));
}


#endif

#endif //MICROJIT_JIT_X86_64_H

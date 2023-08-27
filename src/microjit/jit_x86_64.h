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


#if defined(DEBUG_ENABLED) && defined(VERBOSE_ASSEMBLER_LOG) && !defined(AIN)
#include <iostream>
#define AINL(m_message) std::cout << "[ASM: " << (__LINE__) << "]\t\t" << m_message << "\n"
#define AIN(m_action) std::cout << "[ASM: " << (__LINE__) << "]\t" << &(#m_action)[11] << "\n"; (m_action)
#else
#define AINL(m_message) ((void)0)
#define AIN(m_action) m_action
#endif


namespace microjit {
    class MicroJITCompiler_x86_64 : public MicroJITCompiler {
    private:
        struct BranchInfo : public ThreadUnsafeObject {
            const asmjit::Label begin_of_scope;
            const asmjit::Label end_of_scope;
            const asmjit::Label loop_end_of_scope;
            Ref<BranchInstruction> else_branch{};
            explicit BranchInfo(microjit::Box<asmjit::x86::Assembler> &assembler)
                : begin_of_scope(assembler->newLabel()),
                  end_of_scope(assembler->newLabel()),
                  loop_end_of_scope(assembler->newLabel()) {}
        };
        struct BranchesReport : public ThreadUnsafeObject {
            std::unordered_map<Ref<BranchInstruction>, Ref<BranchInfo>, InstructionHasher<BranchInstruction>> branch_map{};
        };
        struct ScopeInfo {
            Ref<RectifiedScope> scope;
            int64_t iterating;
            Ref<BranchInstruction> branch_instruction;
            Ref<BranchInfo> branch_info;
            bool registered_begin;
        };
//        const x86_64PrimitiveConverter converter{};
    private:
        static Ref<BranchesReport> create_branches_report(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                          const microjit::Ref<microjit::RectifiedFunction> &p_func);
        template<class T>
        static void copy_immediate_primitive(microjit::Box<asmjit::x86::Assembler> &assembler,
                                             const microjit::Ref<T> &p_instruction);
        static void copy_immediate_primitive_internal(microjit::Box<asmjit::x86::Assembler> &assembler,
                                             const Ref<ImmediateValue>& p_value);
        template<class T>
        static void copy_immediate(microjit::Box<asmjit::x86::Assembler> &assembler,
                                   const microjit::Ref<T> &p_instruction);
        static void copy_immediate_internal(microjit::Box<asmjit::x86::Assembler> &assembler,
                                            const Ref<ImmediateValue>& p_value, const void* p_ctor);
        static void copy_construct_variable_internal(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                     const Type& p_type,
                                                     const void* p_copy_constructor,
                                                     const int64_t& p_offset,
                                                     const int64_t& p_copy_target);
        static void assign_variable_internal(microjit::Box<asmjit::x86::Assembler> &assembler,
                                             const microjit::Type &p_type,
                                             const void* p_operator,
                                             const int64_t& p_offset,
                                             const int64_t& p_copy_target);
        static void assign_variable(microjit::Box<asmjit::x86::Assembler> &assembler,
                                    const microjit::Ref<microjit::AssignInstruction> &p_instruction,
                                    const int64_t& p_offset,
                                    const int64_t& p_copy_target);
        static void iterative_destructor_call(microjit::Box<asmjit::x86::Assembler> &assembler,
                                              const Ref<StackFrameInfo>& p_frame_info,
                                              const std::stack<ScopeInfo>& p_scope_stack);
        static void single_scope_destructor_call(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                 const microjit::Ref<microjit::MicroJITCompiler::StackFrameInfo> &p_frame_info,
                                                 const microjit::MicroJITCompiler_x86_64::ScopeInfo &p_current_scope);
        static void trampoline_caller(const std::function<void(microjit::VirtualStack*)>* p_trampoline, VirtualStack* p_stack);
        static void copy_construct_atomic_expression(Box<asmjit::x86::Assembler> &assembler,
                                                     const Ref<StackFrameInfo>& p_frame_report,
                                                     const Ref<CopyConstructInstruction> &p_instruction);
        static void assign_atomic_expression(Box<asmjit::x86::Assembler> &assembler,
                                             const Ref<StackFrameInfo>& p_frame_report,
                                             const Ref<AssignInstruction> &p_instruction);
        static void assign_binary_atomic_expression(Box<asmjit::x86::Assembler> &assembler,
                                                    const Ref<StackFrameInfo>& p_frame_report,
                                                    const Ref<VariableInstruction> &p_target_var,
                                                    const Ref<BinaryOperation> &p_binary);
        static void assign_primitive_binary_atomic_expression(Box<asmjit::x86::Assembler> &assembler,
                                                              const Ref<StackFrameInfo>& p_frame_report,
                                                              const Ref<VariableInstruction> &p_instruction,
                                                              const Ref<PrimitiveBinaryOperation> &p_primitive_binary);
        static void branch_eval_binary_atomic_expression(Box<asmjit::x86::Assembler> &assembler,
                                                        const Ref<StackFrameInfo>& p_frame_report,
                                                        const Ref<BranchesReport>& p_branches_report,
                                                        const Ref<BranchInstruction> &p_target_var,
                                                        const Ref<BranchInfo>& p_branch_info,
                                                        const Ref<BinaryOperation> &p_binary);
        static void branch_eval_primitive_binary_atomic_expression(Box<asmjit::x86::Assembler> &assembler,
                                                                  const Ref<StackFrameInfo>& p_frame_report,
                                                                   const Ref<BranchesReport>& p_branches_report,
                                                                  const Ref<BranchInstruction> &p_instruction,
                                                                   const Ref<BranchInfo>& p_branch_info,
                                                                  const Ref<PrimitiveBinaryOperation> &p_primitive_binary);
    protected:
        CompilationResult compile_internal(const Ref<RectifiedFunction>& p_func) const override;
    public:
        explicit MicroJITCompiler_x86_64(const Ref<MicroJITRuntime>& p_runtime) : MicroJITCompiler(p_runtime) {}
    };
}


template<class T>
void microjit::MicroJITCompiler_x86_64::copy_immediate_primitive(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                                 const microjit::Ref<T> &p_instruction) {
    auto imm = p_instruction->value_reference.template c_style_cast<ImmediateValue>();
    copy_immediate_primitive_internal(assembler, imm);
}

template<class T>
void microjit::MicroJITCompiler_x86_64::copy_immediate(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                       const microjit::Ref<T> &p_instruction) {
    auto imm = p_instruction->value_reference.template c_style_cast<ImmediateValue>();
    copy_immediate_internal(assembler, imm, p_instruction->ctor);
}


#endif

#endif //MICROJIT_JIT_X86_64_H

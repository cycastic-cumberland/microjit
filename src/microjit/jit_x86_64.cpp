//
// Created by cycastic on 8/15/23.
//
#if defined(__x86_64__) || defined(_M_X64)

#include "jit_x86_64.h"
#include "virtual_stack.h"

static constexpr auto rbp = asmjit::x86::rbp;
static constexpr auto rsp = asmjit::x86::rsp;
static constexpr auto rdi = asmjit::x86::rdi;
static constexpr auto rsi = asmjit::x86::rsi;
static constexpr auto rdx = asmjit::x86::rdx;
static constexpr auto rcx = asmjit::x86::rcx;
static constexpr auto rbx = asmjit::x86::rbx;
static constexpr auto rax = asmjit::x86::rax;
static constexpr auto eax = asmjit::x86::eax;
static constexpr auto ebx = asmjit::x86::ebx;
static constexpr auto edi = asmjit::x86::edi;
static constexpr auto esi = asmjit::x86::esi;

static constexpr auto ptrs = int64_t(sizeof(microjit::VirtualStack::StackPtr));

#define VERBOSE_ASSEMBLER_LOG

#if defined(DEBUG_ENABLED) && defined(VERBOSE_ASSEMBLER_LOG)
#include <iostream>

#define AIN(m_action) std::cout << "[ASM: " << (__LINE__) << "]\t" << &(#m_action)[11] << "\n"; (m_action)
#else
#define AIN(m_action) (m_action)
#endif
#define STORE_VRBP asmjit::x86::qword_ptr(rbp, -(ptrs * 2))
#define STORE_VRSP asmjit::x86::qword_ptr(rbp, -(ptrs * 3))
#define LOAD_VRBP asmjit::x86::qword_ptr(rbp, -(ptrs * 4))
#define LOAD_VRSP asmjit::x86::qword_ptr(rbp, -(ptrs * 5))
#define RESIZE_VSTACK(m_amount)                         \
    AIN(assembler->mov(rbx, LOAD_VRSP));                \
    AIN(assembler->sub(rbx, (m_amount)));               \
    AIN(assembler->mov(STORE_VRSP, rbx))

microjit::MicroJITCompiler::CompilationResult
microjit::MicroJITCompiler_x86_64::compile_internal(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto assembly = Ref<Assembly>::make_ref(runtime->get_asmjit_runtime());
    auto& assembler = assembly->assembler;

    AIN(assembler->push(rbp));
    AIN(assembler->mov(rbp, rsp));
    AIN(assembler->sub(rsp, ptrs * 5));



    // Move the VirtualStack pointer from the first argument (rdi)
    // to the first slot of the stack
    AIN(assembler->mov(asmjit::x86::qword_ptr(rbp, -ptrs), rdi));
    AIN(assembler->call(virtual_stack_get_rbp));
    // The address to virtual rbp is now at rbp - 16
    AIN(assembler->mov(STORE_VRBP, rax));
    AIN(assembler->mov(rbx, asmjit::x86::qword_ptr(rax)));
    // The real value of virtual rbp is now at rbp - 32
    AIN(assembler->mov(LOAD_VRBP, rbx));
    AIN(assembler->call(virtual_stack_get_rsp));
    // The address to virtual rbp is now at rsp - 24
    AIN(assembler->mov(STORE_VRSP, rax));
    AIN(assembler->mov(rbx, asmjit::x86::qword_ptr(rax)));
    // The real value of virtual rsp is now at rbp - 40
    AIN(assembler->mov(LOAD_VRSP, rbx));

    // Invoke VirtualStack::capacity
    AIN(assembler->call(virtual_stack_get_capacity));
    // rbx = capacity
    AIN(assembler->mov(rbx, rax));
    // Invoke VirtualStack::allocated
    AIN(assembler->call(virtual_stack_get_allocated));
    // rax = allocated; rbx = capacity

    auto so_test_success = assembler->newLabel();
    AIN(assembler->cmp(rax, rbx));
    AIN(assembler->jle(so_test_success)); // Jump to 'so_test_success' if allocated <= capacity
    // Call 'raise_stack_overflow' if did not jump
    AIN(assembler->call(MicroJITCompiler::raise_stack_overflown));
    AIN(assembler->bind(so_test_success));

    // TODO: get max stack after scope support
    size_t main_scope_stack_size = 0;
    for (const auto& v : p_func->main_scope->get_variables()){
        main_scope_stack_size += v->type.size;
    }

    if (main_scope_stack_size){
        RESIZE_VSTACK(main_scope_stack_size);
    }

    std::unordered_map<size_t, int64_t> variables_vstack_offset_map{};
    const auto& instructions = p_func->main_scope->get_instructions();
    uint32_t main_scope_wise_id_offset = 0;
    int64_t scope_offset = 0;
    int64_t local_offset = 0;

    for (size_t i = 0, s = instructions.size(); i < s; i++){
        const auto& current_instruction = instructions[i];
        switch (current_instruction->get_instruction_type()) {
            case Instruction::IT_DECLARE_VARIABLE: {
                auto as_var_decl = current_instruction.c_style_cast<VariableInstruction>();
                local_offset -= int64_t(as_var_decl->type.size);
                variables_vstack_offset_map[(size_t)as_var_decl.ptr()] = scope_offset + local_offset;
                // It's only job is declare
                break;
            }
            case Instruction::IT_RETURN: {
                auto as_return = current_instruction.c_style_cast<ReturnInstruction>();
                // is void
                if (p_func->return_type.size == 0) break;
                const auto& return_variable = as_return->return_var;
                auto vstack_offset = variables_vstack_offset_map.at((size_t)return_variable.ptr());
                AIN(assembler->mov(rcx, LOAD_VRBP));
                AIN(assembler->mov(rbx, rcx));
                AIN(assembler->sub(rcx, std::abs(vstack_offset)));
                // rbx store the return address, rcx store the variable address
                switch (return_variable->type.size) {
                    case 1:
                        AIN(assembler->mov(asmjit::x86::dl, asmjit::x86::byte_ptr(asmjit::x86::rcx)));
                        AIN(assembler->mov(asmjit::x86::byte_ptr(rbx), asmjit::x86::dl));
                        break;
                    case 2:
                        AIN(assembler->mov(asmjit::x86::dx, asmjit::x86::word_ptr(asmjit::x86::rcx)));
                        AIN(assembler->mov(asmjit::x86::word_ptr(rbx), asmjit::x86::dx));
                        break;
                    case 4:
                        AIN(assembler->mov(asmjit::x86::edx, asmjit::x86::dword_ptr(asmjit::x86::rcx)));
                        AIN(assembler->mov(asmjit::x86::dword_ptr(rbx), asmjit::x86::edx));
                        break;
                    case 8:
                        AIN(assembler->mov(asmjit::x86::rdx, asmjit::x86::qword_ptr(asmjit::x86::rcx)));
                        AIN(assembler->mov(asmjit::x86::qword_ptr(rbx), asmjit::x86::rdx));
                        break;
                    default: {
                        // Move rbx into rdi
                        AIN(assembler->mov(asmjit::x86::rdi, rbx));
                        // Move rcx into rsi
                        AIN(assembler->mov(asmjit::x86::rsi, rcx));
                        // Call copy constructor
                        AIN(assembler->call(return_variable->type.copy_constructor));
                        break;
                    }
                }
                call_destructors(assembler, variables_vstack_offset_map, p_func->main_scope);
                break;
            }
            case Instruction::IT_CONSTRUCT_IMM: {
                auto as_imm_assign = current_instruction.c_style_cast<CopyConstructImmInstruction>();
                auto vstack_offset = variables_vstack_offset_map.at((size_t)as_imm_assign->assign_target.ptr());
                AIN(assembler->mov(rbx, LOAD_VRBP));
                AIN(assembler->sub(rbx, std::abs(vstack_offset)));
                switch (as_imm_assign->assign_target->type.size) {
                    case 1:
                        AIN(assembler->mov(asmjit::x86::byte_ptr(rbx), *(uint8_t*)(as_imm_assign->data)));
                        break;
                    case 2:
                        AIN(assembler->mov(asmjit::x86::word_ptr(rbx), *(uint16_t*)(as_imm_assign->data)));
                        break;
                    case 4:
                        AIN(assembler->mov(asmjit::x86::dword_ptr(rbx), *(uint32_t*)(as_imm_assign->data)));
                        break;
                    case 8:
                        // Somehow 64-bit values require a bit more attention...
                        AIN(assembler->mov(rdx, *(uint64_t*)(as_imm_assign->data)));
                        AIN(assembler->mov(asmjit::x86::qword_ptr(rbx), rdx));
                        break;
                    default: {
                        move_immediate(assembler, as_imm_assign);
                        break;
                    }
                }
                break;
            }
            case Instruction::IT_NONE:
                break;
        }
    }

    AIN(assembler->leave());
    AIN(assembler->ret());

    runtime->get_asmjit_runtime().add(&assembly->callback, &assembly->code);

    return { 0, assembly };
}

void microjit::MicroJITCompiler_x86_64::move_immediate(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                       const microjit::Ref<microjit::CopyConstructImmInstruction> &p_imm) {
    const auto& type_data = p_imm->type;
    // The value to be copied, as byte array
    auto as_byte_array = p_imm->data;
    // Move copy destination (which is rbx) into rdi
    AIN(assembler->mov(rdi, rbx));
    // Allocate immediate value space on real stack
    // Copy the value onto real stack, byte by byte
    AIN(assembler->sub(rsp, type_data.size));
    // Copying by receding chunks did not work, not sure why
    // Copying byte-by-byte would have to do it for now
    for (auto seek = decltype(type_data.size)(); seek < type_data.size; seek++) {
        AIN(assembler->mov(asmjit::x86::byte_ptr(rsp, seek), ((const uint8_t*)as_byte_array)[seek]));
    }
    // Move copy target (which is going to be real rsp) to rsi
    AIN(assembler->mov(rsi, rsp));
    // Call copy constructor
    AIN(assembler->call(type_data.copy_constructor));
    // Return the stack space
    AIN(assembler->add(rsp, type_data.size));
}

void microjit::MicroJITCompiler_x86_64::call_destructors(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                         const std::unordered_map<size_t, int64_t> &p_offset_map,
                                                         const microjit::Ref<microjit::RectifiedScope> &p_scope) {
    for (const auto& var : p_scope->get_variables()){
        auto vstack_offset = p_offset_map.at((size_t)var.ptr());
        AIN(assembler->mov(rbx, LOAD_VRBP));
        AIN(assembler->sub(rbx, std::abs(vstack_offset)));
        AIN(assembler->mov(rdi, rbx));
        AIN(assembler->call(var->type.destructor));
    }
}

#undef STORE_VRBP
#undef STORE_VRSP
#undef LOAD_VRBP
#undef LOAD_VRSP

#endif

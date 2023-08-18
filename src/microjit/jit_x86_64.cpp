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

#define STORE_VRBP asmjit::x86::qword_ptr(rbp, -(ptrs * 2))
#define STORE_VRSP asmjit::x86::qword_ptr(rbp, -(ptrs * 3))
#define LOAD_VRBP asmjit::x86::qword_ptr(rbp, -(ptrs * 4))
#define LOAD_VRSP asmjit::x86::qword_ptr(rbp, -(ptrs * 5))
#define RESIZE_VSTACK(m_amount)                         \
    AIN(assembler->mov(rbx, LOAD_VRSP));                \
    AIN(assembler->sub(rbx, (m_amount)));               \
    AIN(assembler->mov(STORE_VRSP, rbx))

#define VAR_COPY(m_var)                                                                             \
    if (!m_var->type.is_fundamental){                                                               \
        AIN(assembler->mov(asmjit::x86::rdi, rbx));                                                 \
        AIN(assembler->mov(asmjit::x86::rsi, rcx));                                                 \
        AIN(assembler->call(m_var->type.copy_constructor));                                         \
    } else {                                                                                        \
        switch (m_var->type.size) {                                                                 \
            case 1:                                                                                 \
                AIN(assembler->mov(asmjit::x86::dl, asmjit::x86::byte_ptr(asmjit::x86::rcx)));      \
                AIN(assembler->mov(asmjit::x86::byte_ptr(rbx), asmjit::x86::dl));                   \
                break;                                                                              \
            case 2:                                                                                 \
                AIN(assembler->mov(asmjit::x86::dx, asmjit::x86::word_ptr(asmjit::x86::rcx)));      \
                AIN(assembler->mov(asmjit::x86::word_ptr(rbx), asmjit::x86::dx));                   \
                break;                                                                              \
            case 4:                                                                                 \
                AIN(assembler->mov(asmjit::x86::edx, asmjit::x86::dword_ptr(asmjit::x86::rcx)));    \
                AIN(assembler->mov(asmjit::x86::dword_ptr(rbx), asmjit::x86::edx));                 \
                break;                                                                              \
            case 8:                                                                                 \
                AIN(assembler->mov(asmjit::x86::rdx, asmjit::x86::qword_ptr(asmjit::x86::rcx)));    \
                AIN(assembler->mov(asmjit::x86::qword_ptr(rbx), asmjit::x86::rdx));                 \
                break;                                                                              \
        }                                                                                           \
    }


microjit::MicroJITCompiler::CompilationResult
microjit::MicroJITCompiler_x86_64::compile_internal(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto assembly = Ref<Assembly>::make_ref(runtime->get_asmjit_runtime());
    auto& assembler = assembly->assembler;

    AINL("Prologue");
    AIN(assembler->push(rbp));
    AIN(assembler->mov(rbp, rsp));
    AIN(assembler->sub(rsp, ptrs * 5));



    AINL("Setting up virtual stack");
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

    AINL("Stack overflow test");
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

    auto frame_report = create_frame_report(p_func);
    const auto& offset_map = frame_report->variable_map;
    if (frame_report->max_frame_size){
        AINL("Resizing virtual stack");
        RESIZE_VSTACK(frame_report->max_frame_size);
    }


    std::stack<ScopeInfo> scope_stack{};
    scope_stack.push(ScopeInfo{p_func->main_scope, -1, Box<asmjit::Label>()});
    while (!scope_stack.empty()){
        auto current = scope_stack.top();
        current.iterating++;
        scope_stack.pop();
        if (current.label.is_null()){
            current.label = Box<asmjit::Label>::make_box(asmjit::Label(assembler->newLabel()));
            AIN(assembler->bind(*(current.label.ptr())));
        }

        const auto& instructions = current.scope->get_instructions();
        for (auto s = int64_t(instructions.size()); current.iterating < s; current.iterating++){
            const auto& current_instruction = instructions[current.iterating];
            bool loop_break = false;
            switch (current_instruction->get_instruction_type()) {
                case Instruction::IT_CONSTRUCT: {
                    AINL("Constructing variable " << (size_t)current_instruction.ptr());
                    auto as_ctor = current_instruction.c_style_cast<ConstructInstruction>();
                    auto vstack_offset = offset_map.at(as_ctor->target_variable);
                    AIN(assembler->mov(rdi, LOAD_VRBP));
                    AIN(assembler->sub(rdi, std::abs(vstack_offset)));
                    AIN(assembler->call(as_ctor->ctor));
                    break;
                }
                case Instruction::IT_COPY_CONSTRUCT: {
                    auto as_cc = current_instruction.c_style_cast<CopyConstructInstruction>();
                    AINL("Copy constructing variable " << (size_t)as_cc->target_variable.ptr());
                    auto vstack_offset = offset_map.at(as_cc->target_variable);
                    switch (as_cc->value_reference->get_value_type()) {
                        case Value::VAL_IMMEDIATE: {
                            AIN(assembler->mov(rdi, LOAD_VRBP));
                            AIN(assembler->sub(rdi, std::abs(vstack_offset)));
                            if (as_cc->target_variable->type.is_fundamental)
                                copy_immediate_primitive(assembler, as_cc);
                            else copy_immediate(assembler, as_cc);
                            break;
                        }
                        case Value::VAL_ARGUMENT: {
                            auto as_arg = as_cc->value_reference.safe_cast<ArgumentValue>();
                            auto arg_offset = p_func->arguments->argument_offsets()[as_arg->argument_index];
                            arg_offset += p_func->return_type.size;
                            copy_variable(assembler, as_cc, vstack_offset, arg_offset);
                            break;
                        }
                        case Value::VAL_VARIABLE: {
                            auto as_var_val = as_cc->value_reference.c_style_cast<VariableValue>();
                            auto copy_target_offset = offset_map.at(as_var_val->variable);
                            copy_variable(assembler, as_cc, vstack_offset,
                                          copy_target_offset);
                            break;
                        }
                    }
                    break;
                }
                case Instruction::IT_ASSIGN: {
                    auto as_assign = current_instruction.c_style_cast<AssignInstruction>();
                    AINL("Assigning variable " << (size_t)as_assign->target_variable.ptr());
                    auto vstack_offset = offset_map.at(as_assign->target_variable);
                    switch (as_assign->value_reference->get_value_type()) {
                        case Value::VAL_IMMEDIATE: {
                            AIN(assembler->mov(rdi, LOAD_VRBP));
                            AIN(assembler->sub(rdi, std::abs(vstack_offset)));
                            if (as_assign->target_variable->type.is_fundamental)
                                copy_immediate_primitive(assembler, as_assign);
                            else copy_immediate(assembler, as_assign);
                            break;
                        }
                        case Value::VAL_ARGUMENT: {
                            auto as_arg = as_assign->value_reference.c_style_cast<ArgumentValue>();
                            auto arg_offset = p_func->arguments->argument_offsets()[as_arg->argument_index];
                            arg_offset += p_func->return_type.size;
                            assign_variable(assembler, as_assign, vstack_offset, arg_offset);
                            break;
                        }
                        case Value::VAL_VARIABLE: {
                            auto as_var_val = as_assign->value_reference.c_style_cast<VariableValue>();
                            auto copy_target_offset = offset_map.at(as_var_val->variable);
                            assign_variable(assembler, as_assign, vstack_offset,
                                            copy_target_offset);
                            break;
                        }
                    }
                    break;
                }
                case Instruction::IT_RETURN: {
                    auto as_return = current_instruction.c_style_cast<ReturnInstruction>();
                    AINL("Returning variable " << (size_t)as_return->return_var.ptr());
                    // is void
                    if (p_func->return_type.size == 0) break;
                    const auto& return_variable = as_return->return_var;
                    auto vstack_offset = offset_map.at(return_variable);
                    AIN(assembler->mov(rcx, LOAD_VRBP));
                    AIN(assembler->mov(rbx, rcx));
                    AIN(assembler->sub(rcx, std::abs(vstack_offset)));

                    VAR_COPY(return_variable);

                    AINL("Destructing all stack items");
                    // Push the current frame so it can be destroyed
                    scope_stack.push(current);
                    iterative_destructor_call(assembler, frame_report, scope_stack);
//                    arguments_destructor_call(assembler, p_func->arguments);
                    loop_break = true;
                    break;
                }
                case Instruction::IT_SCOPE_CREATE: {
                    AINL("Creating new scope");
                    scope_stack.push(current);
                    scope_stack.push(
                            ScopeInfo{current_instruction.c_style_cast<ScopeCreateInstruction>()->scope,
                                      -1, Box<asmjit::Label>()});
                    loop_break = true;
                    break;
                }
                case Instruction::IT_NONE:
                case Instruction::IT_DECLARE_VARIABLE:
                default:
                    break;
            }
            if (loop_break) break;
        }
    }

    AIN(assembler->leave());
    AIN(assembler->ret());

    runtime->get_asmjit_runtime().add(&assembly->callback, &assembly->code);

    return { 0, assembly };
}
void microjit::MicroJITCompiler_x86_64::copy_variable(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                      const microjit::Ref<microjit::CopyConstructInstruction> &p_instruction,
                                                      const int64_t& p_offset,
                                                      const int64_t& p_copy_target) {
    const auto& type_data = p_instruction->target_variable->type;
    auto arg = p_instruction->value_reference.c_style_cast<ArgumentValue>();
    AIN(assembler->mov(rbx, LOAD_VRBP));
    AIN(assembler->mov(rcx, rbx));
    AIN(assembler->sub(rbx, std::abs(p_offset)));
    // argument_offset = return_size + relative_offset
    if (p_copy_target >= 0)
        AIN(assembler->add(rcx, p_copy_target));
    else
        AIN(assembler->sub(rcx, std::abs(p_copy_target)));
    VAR_COPY(p_instruction->target_variable);
}


void microjit::MicroJITCompiler_x86_64::assign_variable(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                        const microjit::Ref<microjit::AssignInstruction> &p_instruction,
                                                        const int64_t &p_offset, const int64_t &p_copy_target) {
    const auto& type_data = p_instruction->target_variable->type;
    auto arg = p_instruction->value_reference.c_style_cast<ArgumentValue>();
    AIN(assembler->mov(rbx, LOAD_VRBP));
    AIN(assembler->mov(rcx, rbx));
    AIN(assembler->sub(rbx, std::abs(p_offset)));
    if (p_copy_target >= 0)
        AIN(assembler->add(rcx, p_copy_target));
    else
        AIN(assembler->sub(rcx, std::abs(p_copy_target)));
    const auto& target_var = p_instruction->target_variable;
    if (!target_var->type.is_fundamental){
        AIN(assembler->mov(asmjit::x86::rdi, rbx));
        AIN(assembler->mov(asmjit::x86::rsi, rcx));
        AIN(assembler->call(p_instruction->ctor));
    } else {
        switch (target_var->type.size) {
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
        }
    }
}

void microjit::MicroJITCompiler_x86_64::iterative_destructor_call(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                                  const microjit::Ref<microjit::MicroJITCompiler::StackFrameInfo> &p_frame_info,
                                                                  std::stack<ScopeInfo> &p_scope_stack) {
    while (!p_scope_stack.empty()){
        auto current = p_scope_stack.top();
        p_scope_stack.pop();
        for (const auto& var : current.scope->get_variables()){
            if (var->type.is_fundamental) continue;
            // If variable is yet to be constructed
            if (var->get_scope_offset() > current.iterating) break;
            auto vstack_offset = p_frame_info->variable_map.at(var);
            AIN(assembler->mov(rdi, LOAD_VRBP));
            AIN(assembler->sub(rdi, std::abs(vstack_offset)));
            AIN(assembler->call(var->type.destructor));
        }
    }
}

#undef VAR_COPY
#undef STORE_VRBP
#undef STORE_VRSP
#undef LOAD_VRBP
#undef LOAD_VRSP

#endif

//
// Created by cycastic on 8/15/23.
//
#if defined(__x86_64__) || defined(_M_X64)

#include "jit_x86_64.h"
#include "virtual_stack.h"

// I've yet to test this, but this probably won't work with stdcall

static constexpr auto rbp = asmjit::x86::rbp;
static constexpr auto rsp = asmjit::x86::rsp;
static constexpr auto rdi = asmjit::x86::rdi;
static constexpr auto rsi = asmjit::x86::rsi;
static constexpr auto rax = asmjit::x86::rax;
static constexpr auto rbx = asmjit::x86::rbx;
static constexpr auto rcx = asmjit::x86::rcx;
static constexpr auto rdx = asmjit::x86::rdx;

static constexpr auto xmm0 = asmjit::x86::xmm0;
static constexpr auto xmm1 = asmjit::x86::xmm1;

static constexpr auto ptrs = int64_t(sizeof(microjit::VirtualStack::StackPtr));

#define VSTACK_LOC asmjit::x86::qword_ptr(rbp, -ptrs)
#define LOAD_VRBP_LOC (-(ptrs * 4))
#define STORE_VRBP asmjit::x86::qword_ptr(rbp, -(ptrs * 2))
#define STORE_VRSP asmjit::x86::qword_ptr(rbp, -(ptrs * 3))
#define LOAD_VRBP asmjit::x86::qword_ptr(rbp, LOAD_VRBP_LOC)
#define LOAD_VRSP asmjit::x86::qword_ptr(rbp, -(ptrs * 5))

#define VSTACK_SETUP()                                      \
    AIN(assembler->call(virtual_stack_get_rbp));            \
    AIN(assembler->mov(STORE_VRBP, rax));                   \
    AIN(assembler->mov(rbx, asmjit::x86::qword_ptr(rax)));  \
    AIN(assembler->mov(LOAD_VRBP, rbx));                    \
    AIN(assembler->call(virtual_stack_get_rsp));            \
    AIN(assembler->mov(STORE_VRSP, rax));                   \
    AIN(assembler->mov(rbx, asmjit::x86::qword_ptr(rax)));  \
    AIN(assembler->mov(LOAD_VRSP, rbx));

#define RESIZE_VSTACK(m_amount)                             \
    AIN(assembler->mov(rbx, LOAD_VRSP));                    \
    AIN(assembler->sub(rbx, (m_amount)));                   \
    AIN(assembler->mov(rcx, STORE_VRSP));                   \
    AIN(assembler->mov(asmjit::x86::qword_ptr(rcx), rbx));  \
    AIN(assembler->mov(LOAD_VRSP, rbx))

#define VAR_COPY(m_size, m_primitive, m_copy)                                                    \
    if (!(m_primitive)){                                                                            \
        AIN(assembler->mov(asmjit::x86::rdi, rbx));                                                 \
        AIN(assembler->mov(asmjit::x86::rsi, rcx));                                                 \
        AIN(assembler->call((m_copy)));                                                             \
    } else {                                                                                        \
        switch ((m_size)) {                                                                         \
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
microjit::MicroJITCompiler_x86_64::compile_internal(const microjit::Ref<microjit::RectifiedFunction> &p_func) const {
    Ref<Assembly> assembly{};
    {
        std::lock_guard<std::mutex> guard(runtime->get_mutex());
        assembly = Ref<Assembly>::make_ref(runtime->get_asmjit_runtime());
    }
    auto& assembler = assembly->assembler;
    AINL("Prologue");
    AIN(assembler->push(rbp));
    AIN(assembler->mov(rbp, rsp));
    AIN(assembler->sub(rsp, ptrs * 5));

    AINL("Setting up virtual stack");
    // Move the VirtualStack pointer from the first argument (rdi)
    // to the first slot of the stack
    AIN(assembler->mov(VSTACK_LOC, rdi));
    VSTACK_SETUP();
#ifdef RUN_STACK_OVERFLOW_CHECK
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
#endif

    const auto& function_arguments = p_func->arguments;

    auto frame_report = create_frame_report(p_func);
    const auto& offset_map = frame_report->variable_map;
    if (frame_report->max_frame_size){
        AINL("Resizing virtual stack");
        RESIZE_VSTACK(frame_report->max_frame_size);
    }

    auto exit_label = assembler->newLabel();
    std::stack<ScopeInfo> scope_stack{};
    scope_stack.push(ScopeInfo{p_func->main_scope, -1, Box<asmjit::Label>()});
    while (!scope_stack.empty()){
        auto current = scope_stack.top();
        AINL("Entering scope " << std::to_string((size_t)current.scope.ptr()));
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
                    auto type = as_cc->target_variable->type;
                    AINL("Copy constructing variable " << (size_t)as_cc->target_variable.ptr());
                    auto vstack_offset = offset_map.at(as_cc->target_variable);
                    switch (as_cc->value_reference->get_value_type()) {
                        case Value::VAL_IMMEDIATE: {
                            AIN(assembler->mov(rdi, LOAD_VRBP));
                            AIN(assembler->sub(rdi, std::abs(vstack_offset)));
                            if (as_cc->target_variable->type.is_primitive)
                                copy_immediate_primitive(assembler, as_cc);
                            else copy_immediate(assembler, as_cc);
                            break;
                        }
                        case Value::VAL_ARGUMENT: {
                            auto as_arg = as_cc->value_reference.safe_cast<ArgumentValue>();
                            auto arg_offset = function_arguments->argument_offsets()[as_arg->argument_index];
                            arg_offset += p_func->return_type.size;
                            copy_construct_variable_internal(assembler, type, as_cc->ctor, vstack_offset, arg_offset);
                            break;
                        }
                        case Value::VAL_VARIABLE: {
                            auto as_var_val = as_cc->value_reference.c_style_cast<VariableValue>();
                            auto copy_target_offset = offset_map.at(as_var_val->variable);
                            copy_construct_variable_internal(assembler, type, as_cc->ctor, vstack_offset,
                                                             copy_target_offset);
                            break;
                        }
                        case Value::VAL_EXPRESSION: {
                            copy_construct_atomic_expression(assembler, frame_report, as_cc);
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
                            if (as_assign->target_variable->type.is_primitive)
                                copy_immediate_primitive(assembler, as_assign);
                            else copy_immediate(assembler, as_assign);
                            break;
                        }
                        case Value::VAL_ARGUMENT: {
                            auto as_arg = as_assign->value_reference.c_style_cast<ArgumentValue>();
                            auto arg_offset = function_arguments->argument_offsets()[as_arg->argument_index];
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
                        case Value::VAL_EXPRESSION: {
                            assign_atomic_expression(assembler, frame_report, as_assign);
                        }
                    }
                    break;
                }
                case Instruction::IT_RETURN: {
                    auto as_return = current_instruction.c_style_cast<ReturnInstruction>();
                    AINL("Returning variable " << (size_t)as_return->return_var.ptr());
                    // is void
                    if (p_func->return_type.size > 0) {
                        const auto& return_variable = as_return->return_var;
                        const auto type = return_variable->type;
                        auto vstack_offset = offset_map.at(return_variable);
                        AIN(assembler->mov(rcx, LOAD_VRBP));
                        AIN(assembler->mov(rbx, rcx));
                        AIN(assembler->sub(rcx, std::abs(vstack_offset)));
                        VAR_COPY(type.size, type.is_primitive, type.copy_constructor);
                    }

                    AINL("Destructing all stack items");
                    // Push the current frame so it can be destroyed
                    scope_stack.push(current);
                    iterative_destructor_call(assembler, frame_report, scope_stack);
                    scope_stack.pop();
                    AIN(assembler->jmp(exit_label));
                    loop_break = true;
                    current.iterating = -1;
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
                case Instruction::IT_CONVERT: {
                    auto as_convert = current_instruction.c_style_cast<ConvertInstruction>();
                    AINL("Converting variable " << std::to_string((size_t)as_convert->from_var.ptr()) << " to variable " << std::to_string((size_t)as_convert->to_var.ptr()));
                    auto from_offset = offset_map.at(as_convert->from_var);
                    auto to_offset = offset_map.at(as_convert->to_var);
                    AIN(assembler->mov(rdi, LOAD_VRBP));
                    AIN(assembler->mov(rsi, rdi));
                    AIN(assembler->sub(rdi, std::abs(from_offset)));
                    AIN(assembler->sub(rsi, std::abs(to_offset)));
                    AIN(assembler->call(as_convert->converter));
                    break;
                }
                case Instruction::IT_PRIMITIVE_CONVERT: {
                    auto as_convert = current_instruction.c_style_cast<PrimitiveConvertInstruction>();
                    auto from_offset = offset_map.at(as_convert->from_var);
                    auto to_offset = offset_map.at(as_convert->to_var);
                    auto converter_cb = converter.get_handler(as_convert->converter);
                    converter_cb(assembler, LOAD_VRBP_LOC, from_offset, to_offset);
                    break;
                }
                case Instruction::IT_INVOKE_JIT: {
                    auto as_invocation = current_instruction.c_style_cast<InvokeJitInstruction>();
                    const auto& p_target_func = as_invocation->target_function;
                    AINL("Invoking JIT function " << std::to_string((size_t)as_invocation->target_function.ptr()));
                    // Call's prologue
                    // r11 now hold old vrbp
                    AIN(assembler->mov(asmjit::x86::r11, LOAD_VRBP));
                    AIN(assembler->mov(rdi, VSTACK_LOC));
                    AIN(assembler->mov(rsi, p_target_func->return_type.size + as_invocation->arguments_total_size));
                    AIN(assembler->call(virtual_stack_create_stack_frame));
                    // Cache the new stack pointers
                    AIN(assembler->mov(rdi, VSTACK_LOC));
                    VSTACK_SETUP()
                    // r10 now hold new vrbp
                    AIN(assembler->mov(asmjit::x86::r10, LOAD_VRBP));
                    for (const auto& arg : as_invocation->passed_arguments->values){
                        switch (arg->get_value_type()) {
                            case Value::VAL_IMMEDIATE: {
                                auto as_imm = arg.c_style_cast<ImmediateValue>();
                                AIN(assembler->sub(asmjit::x86::r10, as_imm->imm_type.size));
                                AIN(assembler->mov(rdi, asmjit::x86::r10));
                                if (as_imm->imm_type.is_primitive)
                                    copy_immediate_primitive_internal(assembler, as_imm);
                                else copy_immediate_internal(assembler, as_imm, as_imm->imm_type.copy_constructor);
                                break;
                            }
                            case Value::VAL_ARGUMENT: {
                                auto as_arg = arg.c_style_cast<ArgumentValue>();
                                auto idx = as_arg->argument_index;
                                const auto type = function_arguments->argument_types()[idx];
                                auto arg_size = type.size;
                                auto arg_offset = function_arguments->argument_offsets()[idx];
                                arg_offset += p_func->return_type.size;
                                AIN(assembler->sub(asmjit::x86::r10, arg_size));
                                AIN(assembler->mov(rdi, asmjit::x86::r10));
                                AIN(assembler->mov(rsi, asmjit::x86::r11));
                                AIN(assembler->add(rsi, arg_offset));
                                // TODO: primitive support
                                AIN(assembler->call(type.copy_constructor));
                                break;
                            }
                            case Value::VAL_VARIABLE: {
                                auto as_var = arg.c_style_cast<VariableValue>();
                                auto stack_offset = frame_report->variable_map.at(as_var->variable);
                                const auto type = as_var->variable->type;
                                AIN(assembler->sub(asmjit::x86::r10, type.size));
                                AIN(assembler->mov(rdi, asmjit::x86::r10));
                                AIN(assembler->mov(rsi, asmjit::x86::r11));
                                AIN(assembler->sub(rsi, std::abs(stack_offset)));
                                // TODO: primitive support
                                AIN(assembler->call(type.copy_constructor));
                                break;
                            }
                            case Value::VAL_EXPRESSION:
                                MJ_RAISE("Passing expression as argument is not supported. Evaluate them first.");
                        }
                    }
                    AIN(assembler->sub(asmjit::x86::r10, as_invocation->target_function->return_type.size));
                    // The stack setup process is finished
                    // Load trampoline to rdi
                    AIN(assembler->mov(rdi, (size_t)(as_invocation->target_function->trampoline)));
                    // Load VirtualStack to rsi
                    AIN(assembler->mov(rsi, VSTACK_LOC));
                    // Call the trampoline
                    // Note: I have to create a new stack frame again,
                    // or it would not work with clang
                    // I'm not going to add a macro here,
                    // creating a new frame does not hurt the performance that much anyway
                    AIN(assembler->push(rbp));
                    AIN(assembler->mov(rbp, rsp));
                    AIN(assembler->call(trampoline_caller));
                    // Collapse the stack frame
                    AIN(assembler->leave());
                    // Call's epilogue
                    // Cleanup the arguments
                    //
                    // r10 now hold new vrbp
                    AIN(assembler->mov(asmjit::x86::r10, LOAD_VRBP));
                    AIN(assembler->mov(rdi, VSTACK_LOC));
                    AIN(assembler->call(virtual_stack_leave_stack_frame));
                    // Cache the collapsed stack pointers
                    AIN(assembler->mov(rdi, VSTACK_LOC));
                    VSTACK_SETUP()
                    // r11 now hold original vrbp
                    AIN(assembler->mov(asmjit::x86::r11, LOAD_VRBP));
                    for (const auto& arg : as_invocation->passed_arguments->values) {
                        Box<Type> type{};
                        switch (arg->get_value_type()) {
                            case Value::VAL_IMMEDIATE: {
                                auto as_imm = arg.c_style_cast<ImmediateValue>();
                                type = Box<Type>::make_box(as_imm->imm_type);
                                break;
                            }
                            case Value::VAL_ARGUMENT: {
                                auto as_arg = arg.c_style_cast<ArgumentValue>();
                                auto idx = as_arg->argument_index;
                                type = Box<Type>::make_box(function_arguments->argument_types()[idx]);
                                break;
                            }
                            case Value::VAL_VARIABLE: {
                                auto as_var = arg.c_style_cast<VariableValue>();
                                type = Box<Type>::make_box(as_var->variable->type);
                                break;
                            }
                            case Value::VAL_EXPRESSION:
                                MJ_RAISE("I didn't think it was possible to reach this...");
                        }
                        AIN(assembler->sub(asmjit::x86::r10, type->size));
                        if (!type->is_primitive){
                            AIN(assembler->mov(rdi, asmjit::x86::r10));
                            AIN(assembler->call(type->destructor));
                        }
                    }
                    // The function collapse the stack frame before copying the return value
                    // There's no stack push in-between so there should be no problem
                    // If there is... you know where to look
                    const auto func_ret_type = p_target_func->return_type;
                    if (func_ret_type.size > 0) {
                        AIN(assembler->sub(asmjit::x86::r10, func_ret_type.size));
                        // Copy the return value (if needed)
                        auto return_var = as_invocation->return_variable;
                        if (return_var.is_valid()){
                            // rax now hold original vrbp
                            AIN(assembler->mov(rdi, asmjit::x86::r11));
                            // Return variables are copy constructed,
                            // so make sure not to construct them beforehand
                            auto ret_var_offset = frame_report->variable_map.at(return_var);
                            // Construct target in rdi
                            AIN(assembler->sub(rdi, std::abs(ret_var_offset)));
                            // Copy target in rsi
                            AIN(assembler->mov(rsi, asmjit::x86::r10));
                            // Copy return value into variable
                            AIN(assembler->call(return_var->type.copy_constructor));
                        }
                        if (!func_ret_type.is_primitive) {
                            // Cleanup the return value (if there's any)
                            AIN(assembler->mov(rdi, asmjit::x86::r10));
                            AIN(assembler->call(func_ret_type.destructor));
                        }
                    }
                    break;
                }
                case Instruction::IT_NONE:
                case Instruction::IT_DECLARE_VARIABLE:
                default:
                    break;
            }
            if (loop_break) break;
            else if (current.iterating == s - 1){
                // Currently at the last instruction,
                // call destructors
                single_scope_destructor_call(assembler, frame_report, current);
            }
        }
    }

    AINL("Epilogue");
    assembler->bind(exit_label);
    AIN(assembler->leave());
    AIN(assembler->ret());

    {
        std::lock_guard<std::mutex> guard(runtime->get_mutex());
        runtime->get_asmjit_runtime().add(&assembly->callback, &assembly->code);
    }
    return { 0, assembly };
}
void microjit::MicroJITCompiler_x86_64::copy_construct_variable_internal(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                                         const Type& p_type,
                                                                         const void* p_copy_constructor,
                                                                         const int64_t& p_offset,
                                                                         const int64_t& p_copy_target) {
    AIN(assembler->mov(rbx, LOAD_VRBP));
    AIN(assembler->mov(rcx, rbx));
    AIN(assembler->sub(rbx, std::abs(p_offset)));
    // argument_offset = return_size + relative_offset
    if (p_copy_target >= 0) {
        AIN(assembler->add(rcx, p_copy_target));
    } else {
        AIN(assembler->sub(rcx, std::abs(p_copy_target)));
    }
    VAR_COPY(p_type.size, p_type.is_primitive, p_copy_constructor);
}

void microjit::MicroJITCompiler_x86_64::assign_variable_internal(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                                 const microjit::Type &p_type, const void *p_operator,
                                                                 const int64_t &p_offset, const int64_t &p_copy_target) {
    AIN(assembler->mov(rbx, LOAD_VRBP));
    AIN(assembler->mov(rcx, rbx));
    AIN(assembler->sub(rbx, std::abs(p_offset)));
    if (p_copy_target >= 0) {
        AIN(assembler->add(rcx, p_copy_target));
    } else {
        AIN(assembler->sub(rcx, std::abs(p_copy_target)));
    }
    VAR_COPY(p_type.size, p_type.is_primitive, p_operator);
}

void microjit::MicroJITCompiler_x86_64::assign_variable(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                        const microjit::Ref<microjit::AssignInstruction> &p_instruction,
                                                        const int64_t &p_offset, const int64_t &p_copy_target) {
    assign_variable_internal(assembler, p_instruction->target_variable->type, p_instruction->ctor,
                             p_offset, p_copy_target);
}

void microjit::MicroJITCompiler_x86_64::iterative_destructor_call(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                                  const microjit::Ref<microjit::MicroJITCompiler::StackFrameInfo> &p_frame_info,
                                                                  const std::stack<ScopeInfo> &p_scope_stack) {
    auto scope_stack = p_scope_stack;
    while (!scope_stack.empty()){
        auto current = scope_stack.top();
        scope_stack.pop();
        for (const auto& var : current.scope->get_variables()){
            if (var->type.is_primitive) continue;
            // If variable is yet to be constructed
            if (var->get_scope_offset() > current.iterating) break;
            auto vstack_offset = p_frame_info->variable_map.at(var);
            AIN(assembler->mov(rdi, LOAD_VRBP));
            AIN(assembler->sub(rdi, std::abs(vstack_offset)));
            AIN(assembler->call(var->type.destructor));
        }
    }
}

void microjit::MicroJITCompiler_x86_64::single_scope_destructor_call(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                                     const microjit::Ref<microjit::MicroJITCompiler::StackFrameInfo> &p_frame_info,
                                                                     const microjit::MicroJITCompiler_x86_64::ScopeInfo &p_current_scope) {

    for (const auto& var : p_current_scope.scope->get_variables()){
        if (var->type.is_primitive) continue;
        // If variable is yet to be constructed
        if (var->get_scope_offset() > p_current_scope.iterating) break;
        auto vstack_offset = p_frame_info->variable_map.at(var);
        AIN(assembler->mov(rdi, LOAD_VRBP));
        AIN(assembler->sub(rdi, std::abs(vstack_offset)));
        AIN(assembler->call(var->type.destructor));
    }
}

void microjit::MicroJITCompiler_x86_64::copy_immediate_primitive_internal(microjit::Box<asmjit::x86::Assembler> &assembler,
                              const microjit::Ref<microjit::ImmediateValue>& p_value){
    auto as_byte_array = p_value->data;
    const auto size = p_value->imm_type.size;
    switch (size) {
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
        default:
            MJ_RAISE("Unsupported primitive size");
    }
}

void microjit::MicroJITCompiler_x86_64::copy_immediate_internal(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                                const microjit::Ref<microjit::ImmediateValue> &p_value,
                                                                const void* p_ctor) {
    const auto type_data = p_value->imm_type;
    // The value to be copied, as byte array
    auto as_byte_array = p_value->data;
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
    AIN(assembler->call(p_ctor));
    // Return the stack space
    AIN(assembler->add(asmjit::x86::rsp, type_data.size));
}

void microjit::MicroJITCompiler_x86_64::trampoline_caller(const std::function<void(microjit::VirtualStack*)> *p_trampoline,
                                                          microjit::VirtualStack *p_stack) {
    p_trampoline->operator()(p_stack);
}

void microjit::MicroJITCompiler_x86_64::assign_atomic_expression(Box<asmjit::x86::Assembler> &assembler,
                                                                 const Ref<StackFrameInfo>& p_frame_report,
                                                                 const Ref<AssignInstruction> &p_instruction) {
    auto as_expr = p_instruction->value_reference.c_style_cast<AbstractOperation>();
    if (AbstractOperation::is_binary(as_expr->operation_type)){
        auto as_binary_op = as_expr.c_style_cast<BinaryOperation>();
        assign_binary_atomic_expression(assembler, p_frame_report, p_instruction->target_variable, as_binary_op);
    } else {
        MJ_RAISE("Unary operations are yet to be support this");
    }
}

void
microjit::MicroJITCompiler_x86_64::copy_construct_atomic_expression(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                                    const microjit::Ref<microjit::MicroJITCompiler::StackFrameInfo> &p_frame_report,
                                                                    const microjit::Ref<microjit::CopyConstructInstruction> &p_instruction) {
    auto as_expr = p_instruction->value_reference.c_style_cast<AbstractOperation>();
    if (AbstractOperation::is_binary(as_expr->operation_type)){
        auto as_binary_op = as_expr.c_style_cast<BinaryOperation>();
        assign_binary_atomic_expression(assembler, p_frame_report, p_instruction->target_variable, as_binary_op);
    } else {
        MJ_RAISE("Unary operations are yet to be support this");
    }
}

void
microjit::MicroJITCompiler_x86_64::assign_binary_atomic_expression(Box<asmjit::x86::Assembler> &assembler,
                                                                   const Ref<StackFrameInfo>& p_frame_report,
                                                                   const Ref<VariableInstruction> &p_target_var,
                                                                   const microjit::Ref<microjit::BinaryOperation> &p_binary) {
    if (p_binary->is_primitive){
        auto as_primitive_binary_op = p_binary.c_style_cast<PrimitiveBinaryOperation>();
        assign_primitive_binary_atomic_expression(assembler, p_frame_report, p_target_var, as_primitive_binary_op);
    } else {
        MJ_RAISE("Non-primitive binary operations are yet to be support this");
    }
}

// DO NOT CHANGE THESE
#define PRIMITIVE_EXPRESSION_LEFT_INTEGER a
#define PRIMITIVE_EXPRESSION_RIGHT_INTEGER b
#define PE_BUFFER rcx
#define PRIMITIVE_EXPRESSION_LEFT_FP 1
#define PRIMITIVE_EXPRESSION_RIGHT_FP 2

#define REP_FP(m_type) asmjit::x86::xmm##m_type
#define REP_U8(m_type) asmjit::x86::m_type##l
#define REP_U16(m_type) asmjit::x86::m_type##x
#define REP_U32(m_type) asmjit::x86::e##m_type##x
#define REP_U64(m_type) asmjit::x86::r##m_type##x

#define MOVE_PRIMITIVE_OPERAND(m_target_type, m_is_float, m_operand, m_float_type, m_int_type)          \
{                                                                                                       \
    static constexpr auto float_type = Type::create<float>();                                           \
    if (m_is_float) {                                                                                   \
        const auto is_fp32 = (m_target_type == float_type);                                             \
        switch (m_operand->get_value_type()) {                                                          \
            case Value::VAL_IMMEDIATE: {                                                                \
                auto as_imm = m_operand.c_style_cast<ImmediateValue>();                                 \
                if (is_fp32){                                                                           \
                    auto as_integer = *(uint32_t*)as_imm->data;                                         \
                    AIN(assembler->push(as_integer));                                                   \
                    AIN(assembler->lea(PE_BUFFER, asmjit::x86::dword_ptr(rsp)));                        \
                    AIN(assembler->movss(REP_FP(m_float_type), asmjit::x86::dword_ptr(PE_BUFFER)));     \
                    AIN(assembler->pop(asmjit::x86::eax));                                              \
                } else {                                                                                \
                    auto as_integer = *(uint64_t*)as_imm->data;                                         \
                    AIN(assembler->push(as_integer));                                                   \
                    AIN(assembler->lea(PE_BUFFER, asmjit::x86::qword_ptr(rsp)));                        \
                    AIN(assembler->movsd(REP_FP(m_float_type), asmjit::x86::qword_ptr(PE_BUFFER)));     \
                    AIN(assembler->pop(asmjit::x86::PE_BUFFER));                                        \
                }                                                                                       \
            break;                                                                                      \
            }                                                                                           \
            case Value::VAL_VARIABLE: {                                                                 \
                auto as_var = m_operand.c_style_cast<VariableValue>();                                  \
                auto curr_var = as_var->variable;                                                       \
                auto curr_vstack_offset = p_frame_report->variable_map.at(curr_var);                    \
                AIN(assembler->mov(asmjit::x86::r10, LOAD_VRBP));                                       \
                AIN(assembler->sub(asmjit::x86::r10, std::abs(curr_vstack_offset)));                    \
                if (is_fp32){                                                                           \
                    AIN(assembler->lea(rax, asmjit::x86::dword_ptr(asmjit::x86::r10)));                 \
                    AIN(assembler->movss(REP_FP(m_float_type), asmjit::x86::dword_ptr(rax)));           \
                } else {                                                                                \
                    AIN(assembler->lea(rax, asmjit::x86::qword_ptr(asmjit::x86::r10)));                 \
                    AIN(assembler->movsd(REP_FP(m_float_type), asmjit::x86::qword_ptr(rax)));           \
                }                                                                                       \
                break;                                                                                  \
            }                                                                                           \
            case Value::VAL_ARGUMENT:                                                                   \
            case Value::VAL_EXPRESSION:                                                                 \
                MJ_RAISE("Unsupported primitive binary operation");                                     \
        }                                                                                               \
    } else {                                                                                            \
        switch (m_operand->get_value_type()) {                                                          \
            case Value::VAL_IMMEDIATE: {                                                                \
                auto as_imm = m_operand.c_style_cast<ImmediateValue>();                                 \
                const auto* data = as_imm->data;                                                        \
                switch (m_target_type.size) {                                                           \
                    case 1:                                                                             \
                        AIN(assembler->mov(REP_U8(m_int_type), *(uint8_t*)data));                       \
                        break;                                                                          \
                    case 2:                                                                             \
                        AIN(assembler->mov(REP_U16(m_int_type), *(uint16_t*)data));                     \
                        break;                                                                          \
                    case 4:                                                                             \
                        AIN(assembler->mov(REP_U32(m_int_type), *(uint32_t*)data));                     \
                        break;                                                                          \
                    case 8:                                                                             \
                        AIN(assembler->mov(REP_U64(m_int_type), *(uint64_t*)data));                     \
                        break;                                                                          \
                    default:                                                                            \
                        MJ_RAISE("Unsupported integer size");                                           \
                }                                                                                       \
                break;                                                                                  \
            }                                                                                           \
        case Value::VAL_VARIABLE: {                                                                     \
            auto as_var = m_operand.c_style_cast<VariableValue>();                                      \
            auto curr_var = as_var->variable;                                                           \
            auto curr_vstack_offset = p_frame_report->variable_map.at(curr_var);                        \
            AIN(assembler->mov(asmjit::x86::r10, LOAD_VRBP));                                           \
            AIN(assembler->sub(asmjit::x86::r10, std::abs(curr_vstack_offset)));                        \
            switch (m_target_type.size) {                                                               \
                case 1:                                                                                 \
                    AIN(assembler->mov(REP_U8(m_int_type), asmjit::x86::byte_ptr(asmjit::x86::r10)));   \
                    break;                                                                              \
                case 2:                                                                                 \
                    AIN(assembler->mov(REP_U16(m_int_type), asmjit::x86::word_ptr(asmjit::x86::r10)));  \
                    break;                                                                              \
                case 4:                                                                                 \
                    AIN(assembler->mov(REP_U32(m_int_type), asmjit::x86::dword_ptr(asmjit::x86::r10))); \
                    break;                                                                              \
                case 8:                                                                                 \
                    AIN(assembler->mov(REP_U64(m_int_type), asmjit::x86::qword_ptr(asmjit::x86::r10))); \
                    break;                                                                              \
                default:                                                                                \
                    MJ_RAISE("Unsupported integer size");                                               \
            }                                                                                           \
            break;                                                                                      \
        }                                                                                               \
        case Value::VAL_ARGUMENT:                                                                       \
        case Value::VAL_EXPRESSION:                                                                     \
            MJ_RAISE("Unsupported primitive binary operation");                                         \
        }                                                                                               \
    }                                                                                                   \
}

#define INTEGER_ARITHMETIC_HELPER(m_left, m_right, m_op, m_size)            \
{                                                                           \
    switch ((m_size)) {                                                     \
        case 1:                                                             \
            AIN(assembler->m_op(REP_U8(m_left), REP_U8(m_right)));          \
            break;                                                          \
        case 2:                                                             \
            AIN(assembler->m_op(REP_U16(m_left), REP_U16(m_right)));        \
            break;                                                          \
        case 4:                                                             \
            AIN(assembler->m_op(REP_U32(m_left), REP_U32(m_right)));        \
            break;                                                          \
        case 8:                                                             \
            AIN(assembler->m_op(REP_U64(m_left), REP_U64(m_right)));        \
            break;                                                          \
        default:                                                            \
            MJ_RAISE("Unsupported");                                        \
    }                                                                       \
}

#define INTEGER_COMPARISON_HELPER(m_left, m_right, m_op, m_size)            \
{                                                                           \
    switch ((m_size)) {                                                     \
        case 1:                                                             \
            AIN(assembler->cmp(REP_U8(m_left), REP_U8(m_right)));           \
            break;                                                          \
        case 2:                                                             \
            AIN(assembler->cmp(REP_U16(m_left), REP_U16(m_right)));         \
            break;                                                          \
        case 4:                                                             \
            AIN(assembler->cmp(REP_U32(m_left), REP_U32(m_right)));         \
            break;                                                          \
        case 8:                                                             \
            AIN(assembler->cmp(REP_U64(m_left), REP_U64(m_right)));         \
            break;                                                          \
        default:                                                            \
            MJ_RAISE("Unsupported");                                        \
    }                                                                       \
    AIN(assembler->m_op(REP_U8(m_left)));                                   \
}

#define INTEGER_SAVE(m_address, m_storage, m_size)                          \
{                                                                           \
    switch ((m_size)) {                                                     \
        case 1:                                                             \
            AIN(assembler->mov(asmjit::x86::byte_ptr(m_address),            \
                REP_U8(m_storage)));                                        \
            break;                                                          \
        case 2:                                                             \
            AIN(assembler->mov(asmjit::x86::word_ptr(m_address),            \
                REP_U16(m_storage)));                                       \
            break;                                                          \
        case 4:                                                             \
            AIN(assembler->mov(asmjit::x86::dword_ptr(m_address),           \
                REP_U32(m_storage)));                                       \
            break;                                                          \
        case 8:                                                             \
            AIN(assembler->mov(asmjit::x86::qword_ptr(m_address),           \
                REP_U64(m_storage)));                                       \
            break;                                                          \
        default:                                                            \
            MJ_RAISE("Unsupported");                                        \
    }                                                                       \
}

#define INTEGER_DIV(m_divisor, m_size, m_op)                                \
{                                                                           \
    AIN(assembler->xor_(rdx, rdx));                                         \
    switch ((m_size)) {                                                     \
        case 1:                                                             \
            AIN(assembler->m_op(REP_U8(m_divisor)));                        \
            break;                                                          \
        case 2:                                                             \
            AIN(assembler->m_op(REP_U16(m_divisor)));                       \
            break;                                                          \
        case 4:                                                             \
            AIN(assembler->m_op(REP_U32(m_divisor)));                       \
            break;                                                          \
        case 8:                                                             \
            AIN(assembler->m_op(REP_U64(m_divisor)));                       \
            break;                                                          \
        default:                                                            \
            MJ_RAISE("Unsupported");                                        \
    }\
}
#define FLOATING_POINT_ARITHMETIC_HELPER(m_left, m_right, m_op, m_size)     \
{                                                                           \
    switch ((m_size)) {                                                     \
        case 4:                                                             \
            AIN(assembler->m_op##ss(REP_FP(m_left), REP_FP(m_right)));      \
            break;                                                          \
        case 8:                                                             \
            AIN(assembler->m_op##sd(REP_FP(m_left), REP_FP(m_right)));      \
            break;                                                          \
        default:                                                            \
            MJ_RAISE("Unsupported floating point precision");               \
    }                                                                       \
}

#define FLOATING_POINT_SAVE(m_address, m_storage, m_size)                   \
{                                                                           \
    switch ((m_size)) {                                                     \
        case 4:                                                             \
            AIN(assembler->movss(dword_ptr(m_address), REP_FP(m_storage))); \
            break;                                                          \
        case 8:                                                             \
            AIN(assembler->movsd(qword_ptr(m_address), REP_FP(m_storage))); \
            break;                                                          \
        default:                                                            \
            MJ_RAISE("Unsupported floating point precision");               \
    }                                                                       \
}

#define FLOATING_POINT_EQU_CMP(m_left, m_right, m_precision)                \
AIN(assembler->ucomi##m_precision(REP_FP(m_left), REP_FP(m_right)))

#define FLOATING_POINT_BASE_CMP(m_left, m_right, m_precision)               \
AIN(assembler->comi##m_precision(REP_FP(m_left), REP_FP(m_right)))

#define FLOATING_POINT_CMP_EQUAL(m_left, m_right, m_size, m_buf, m_first)   \
{                                                                           \
    switch ((m_size)) {                                                     \
        case 4:                                                             \
            FLOATING_POINT_EQU_CMP(m_left, m_right, ss);                    \
            AIN(assembler->m_first(asmjit::x86::al));                       \
            AIN(assembler->mov(asmjit::x86::edx, (m_buf)));                 \
            FLOATING_POINT_EQU_CMP(m_left, m_right, ss);                    \
            AIN(assembler->cmovne(asmjit::x86::eax,                         \
                                  asmjit::x86::edx));                       \
            break;                                                          \
        case 8:                                                             \
            FLOATING_POINT_EQU_CMP(m_left, m_right, sd);                    \
            AIN(assembler->m_first(asmjit::x86::al));                       \
            AIN(assembler->mov(asmjit::x86::edx, (m_buf)));                 \
            FLOATING_POINT_EQU_CMP(m_left, m_right, sd);                    \
            AIN(assembler->cmovne(asmjit::x86::eax,                         \
                                  asmjit::x86::edx));                       \
            break;                                                          \
        default:                                                            \
            MJ_RAISE("Unsupported floating point precision");               \
    }                                                                       \
}

#define FLOATING_POINT_CMP_OTHER(m_left, m_right, m_size, m_cmp)            \
{                                                                           \
    switch ((m_size)) {                                                     \
        case 4:                                                             \
            FLOATING_POINT_BASE_CMP(m_left, m_right, ss);                   \
            AIN(assembler->m_cmp(asmjit::x86::al));                         \
            break;                                                          \
        case 8:                                                             \
            FLOATING_POINT_BASE_CMP(m_left, m_right, sd);                   \
            AIN(assembler->m_cmp(asmjit::x86::al));                         \
            break;                                                          \
        default:                                                            \
            MJ_RAISE("Unsupported floating point precision");               \
    }                                                                       \
}
static void integer_arithmetic(microjit::Box<asmjit::x86::Assembler> &assembler,
                               microjit::AbstractOperation::OperationType p_op,
                               microjit::Type p_operand_type){
    auto data_size = p_operand_type.size;
    switch (p_op) {
        case microjit::AbstractOperation::BINARY_ADD:
            INTEGER_ARITHMETIC_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                      PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                      add, data_size)
            break;
        case microjit::AbstractOperation::BINARY_SUB:
            INTEGER_ARITHMETIC_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                      PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                      sub, data_size)
            break;
        case microjit::AbstractOperation::BINARY_MUL:
            INTEGER_ARITHMETIC_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                      PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                      imul, data_size)
            break;
        case microjit::AbstractOperation::BINARY_DIV:
        case microjit::AbstractOperation::BINARY_MOD: {
            if (microjit::Type::is_signed_integer(p_operand_type))
                INTEGER_DIV(PRIMITIVE_EXPRESSION_RIGHT_INTEGER, data_size, idiv)
            else INTEGER_DIV(PRIMITIVE_EXPRESSION_RIGHT_INTEGER, data_size, div)

            if (p_op == microjit::AbstractOperation::BINARY_DIV) break;
            // If is modulo, move rdx to rax
            AIN(assembler->mov(rax, rdx));
            break;
        }
        default:
            MJ_RAISE("Unsupported");
    }
//    INTEGER_MOVE(PRIMITIVE_EXPRESSION_LEFT_INTEGER, PRIMITIVE_EXPRESSION_RETURN_INTEGER, data_size)
}

static void integer_comparison(microjit::Box<asmjit::x86::Assembler> &assembler,
                               microjit::AbstractOperation::OperationType p_op,
                               microjit::Type p_operand_type) {
    auto data_size = p_operand_type.size;
    auto is_signed = microjit::Type::is_signed_integer(p_operand_type);
    switch (p_op) {
        case microjit::AbstractOperation::BINARY_EQUAL:
            INTEGER_COMPARISON_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                      PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                      sete, data_size)
            break;
        case microjit::AbstractOperation::BINARY_NOT_EQUAL:
            INTEGER_COMPARISON_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                      PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                      setne, data_size)
            break;
        case microjit::AbstractOperation::BINARY_GREATER:
            if (is_signed)
                INTEGER_COMPARISON_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                          PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                          setg, data_size)
            else
                INTEGER_COMPARISON_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                          PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                          seta, data_size)
            break;
        case microjit::AbstractOperation::BINARY_GREATER_EQ:
            if (is_signed)
                INTEGER_COMPARISON_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                          PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                          setge, data_size)
            else
                INTEGER_COMPARISON_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                          PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                          setnb, data_size)
            break;
        case microjit::AbstractOperation::BINARY_LESSER:
            if (is_signed)
                INTEGER_COMPARISON_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                          PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                          setl, data_size)
            else
                INTEGER_COMPARISON_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                          PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                          setb, data_size)
            break;
        case microjit::AbstractOperation::BINARY_LESSER_EQ:
            if (is_signed)
                INTEGER_COMPARISON_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                          PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                          setle, data_size)
            else
                INTEGER_COMPARISON_HELPER(PRIMITIVE_EXPRESSION_LEFT_INTEGER,
                                          PRIMITIVE_EXPRESSION_RIGHT_INTEGER,
                                          setna, data_size)
            break;
        default:
            MJ_RAISE("Unsupported operation");
    }
}

static void floating_point_arithmetic(microjit::Box<asmjit::x86::Assembler> &assembler,
                                      microjit::AbstractOperation::OperationType p_op,
                                      size_t p_data_size){
    switch (p_op) {
        case microjit::AbstractOperation::BINARY_ADD:
            FLOATING_POINT_ARITHMETIC_HELPER(PRIMITIVE_EXPRESSION_LEFT_FP,
                                             PRIMITIVE_EXPRESSION_RIGHT_FP,
                                             add, p_data_size)
            break;
        case microjit::AbstractOperation::BINARY_SUB:
            FLOATING_POINT_ARITHMETIC_HELPER(PRIMITIVE_EXPRESSION_LEFT_FP,
                                             PRIMITIVE_EXPRESSION_RIGHT_FP,
                                             sub, p_data_size)
            break;
        case microjit::AbstractOperation::BINARY_MUL:
            FLOATING_POINT_ARITHMETIC_HELPER(PRIMITIVE_EXPRESSION_LEFT_FP,
                                             PRIMITIVE_EXPRESSION_RIGHT_FP,
                                             mul, p_data_size)
            break;
        case microjit::AbstractOperation::BINARY_DIV:
            FLOATING_POINT_ARITHMETIC_HELPER(PRIMITIVE_EXPRESSION_LEFT_FP,
                                             PRIMITIVE_EXPRESSION_RIGHT_FP,
                                             div, p_data_size)
            break;
        default:
            MJ_RAISE("Unsupported");
    }
//    FLOATING_POINT_MOVE(PRIMITIVE_EXPRESSION_LEFT_FP, PRIMITIVE_EXPRESSION_RETURN_FP, p_data_size)
}

static void floating_point_comparison(microjit::Box<asmjit::x86::Assembler> &assembler,
                                      microjit::AbstractOperation::OperationType p_op,
                                      size_t p_data_size) {

    switch (p_op) {
        case microjit::AbstractOperation::BINARY_EQUAL: {
            FLOATING_POINT_CMP_EQUAL(PRIMITIVE_EXPRESSION_LEFT_FP,
                                     PRIMITIVE_EXPRESSION_RIGHT_FP,
                                     p_data_size, 0, setnp)
            break;
        }
        case microjit::AbstractOperation::BINARY_NOT_EQUAL: {
            FLOATING_POINT_CMP_EQUAL(PRIMITIVE_EXPRESSION_LEFT_FP,
                                     PRIMITIVE_EXPRESSION_RIGHT_FP,
                                     p_data_size, 1, setp)
            break;
        }
        case microjit::AbstractOperation::BINARY_GREATER: {
            FLOATING_POINT_CMP_OTHER(PRIMITIVE_EXPRESSION_LEFT_FP,
                                     PRIMITIVE_EXPRESSION_RIGHT_FP,
                                     p_data_size, seta)
            break;
        }
        case microjit::AbstractOperation::BINARY_GREATER_EQ: {
            FLOATING_POINT_CMP_OTHER(PRIMITIVE_EXPRESSION_LEFT_FP,
                                     PRIMITIVE_EXPRESSION_RIGHT_FP,
                                     p_data_size, setnb)
            break;
        }
        case microjit::AbstractOperation::BINARY_LESSER: {
            FLOATING_POINT_CMP_OTHER(PRIMITIVE_EXPRESSION_LEFT_FP,
                                     PRIMITIVE_EXPRESSION_RIGHT_FP,
                                     p_data_size, setb)
            break;
        }
        case microjit::AbstractOperation::BINARY_LESSER_EQ: {
            FLOATING_POINT_CMP_OTHER(PRIMITIVE_EXPRESSION_LEFT_FP,
                                     PRIMITIVE_EXPRESSION_RIGHT_FP,
                                     p_data_size, setna)
            break;
        }
        default:
            MJ_RAISE("Unsupported operation");
    }
}

void microjit::MicroJITCompiler_x86_64::assign_primitive_binary_atomic_expression(
        Box<asmjit::x86::Assembler> &assembler,
        const Ref<StackFrameInfo>& p_frame_report,
        const Ref<VariableInstruction> &p_target_var,
        const microjit::Ref<microjit::PrimitiveBinaryOperation> &p_primitive_binary) {

    auto left_operand = p_primitive_binary->left_operand;
    auto right_operand = p_primitive_binary->right_operand;
    Box<Type> operand_type_boxed{};
    bool is_operand_floating_point{};
    switch (left_operand->get_value_type()) {
        case Value::VAL_IMMEDIATE: {
            auto as_imm = left_operand.c_style_cast<ImmediateValue>();
            auto type = as_imm->imm_type;
            is_operand_floating_point = Type::is_floating_point(type);
            operand_type_boxed = Box<Type>::make_box(type);
            break;
        }
        case Value::VAL_VARIABLE: {
            auto as_var = left_operand.c_style_cast<VariableValue>();
            auto type = as_var->variable->type;
            is_operand_floating_point = Type::is_floating_point(type);
            operand_type_boxed = Box<Type>::make_box(type);
            break;
        }
        case Value::VAL_ARGUMENT:
        case Value::VAL_EXPRESSION:
            MJ_RAISE("Unsupported");
    }
    const auto operand_type = *(operand_type_boxed.ptr());
    const auto operation_type = p_primitive_binary->operation_type;

    // Move the left operand into the first register
    MOVE_PRIMITIVE_OPERAND(operand_type, is_operand_floating_point, left_operand,
                           PRIMITIVE_EXPRESSION_LEFT_FP, PRIMITIVE_EXPRESSION_LEFT_INTEGER);

    // Move the second operand into the second register
    MOVE_PRIMITIVE_OPERAND(operand_type, is_operand_floating_point, right_operand,
                           PRIMITIVE_EXPRESSION_RIGHT_FP, PRIMITIVE_EXPRESSION_RIGHT_INTEGER);

    auto target_vstack_offset = p_frame_report->variable_map.at(p_target_var);
    AIN(assembler->mov(PE_BUFFER, LOAD_VRBP));
    AIN(assembler->sub(PE_BUFFER, std::abs(target_vstack_offset)));
    // Calculate the stuff
    if (AbstractOperation::operation_return_same(operation_type)){
        if (is_operand_floating_point) {
            floating_point_arithmetic(assembler, operation_type, operand_type.size);
            FLOATING_POINT_SAVE(PE_BUFFER, PRIMITIVE_EXPRESSION_LEFT_FP, operand_type.size)
        } else {
            auto is_signed = Type::is_signed_integer(operand_type);
            integer_arithmetic(assembler, operation_type, operand_type);
            INTEGER_SAVE(PE_BUFFER, PRIMITIVE_EXPRESSION_LEFT_INTEGER, operand_type.size)
        }
    } else if (AbstractOperation::operation_return_bool(operation_type)) {
        if (is_operand_floating_point) {
            floating_point_comparison(assembler, operation_type, operand_type.size);
            AIN(assembler->mov(asmjit::x86::byte_ptr(PE_BUFFER), asmjit::x86::al));
        } else {
            integer_comparison(assembler, operation_type, operand_type);
            AIN(assembler->mov(asmjit::x86::byte_ptr(PE_BUFFER), asmjit::x86::al));
        }
    } else
        MJ_RAISE("Unsupported operation");
}

#undef VAR_COPY
#undef STORE_VRBP
#undef STORE_VRSP
#undef LOAD_VRBP
#undef LOAD_VRSP

#endif

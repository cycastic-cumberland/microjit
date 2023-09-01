//
// Created by cycastic on 8/15/23.
//
#if defined(__x86_64__) || defined(_M_X64)

#include <queue>
#include "jit_x86_64.h"
#include "virtual_stack.h"


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
#define LOAD_VRBP_LOC (-(ptrs * 2))
#define LOAD_VRBP asmjit::x86::qword_ptr(rbp, LOAD_VRBP_LOC)

#define VSTACK_SETUP()                                      \
    AIN(assembler->call(virtual_stack_get_rbp));            \
    AIN(assembler->mov(rbx, asmjit::x86::qword_ptr(rax)));  \
    AIN(assembler->mov(LOAD_VRBP, rbx));

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
            default:                                                                                \
                MJ_RAISE("Unsupported primitive size");                                             \
        }                                                                                           \
    }

microjit::Ref<microjit::MicroJITCompiler_x86_64::BranchesReport>
microjit::MicroJITCompiler_x86_64::create_branches_report(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                          const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto report = new BranchesReport();
    std::queue<Ref<BranchInstruction>> scope_stack{};
    for (const auto& branch : p_func->main_scope->get_branches()) {
        scope_stack.push(branch);
    }
    Ref<BranchInfo> last_branch_info{};
    while (!scope_stack.empty()) {
        auto current_branch = scope_stack.front();
        scope_stack.pop();
        auto new_info = Ref<BranchInfo>::make_ref(assembler);
        if (current_branch->branch_type == BranchInstruction::BRANCH_ELSE) {
            last_branch_info->else_branch = current_branch;
            last_branch_info = nullptr;
        }
        report->branch_map[current_branch] = new_info;
        last_branch_info = new_info;
        for (const auto& branch : current_branch->sub_scope->get_branches()) {
            scope_stack.push(branch);
        }
    }
    return Ref<BranchesReport>::from_uninitialized_object(report);
}


microjit::MicroJITCompiler::CompilationResult
microjit::MicroJITCompiler_x86_64::compile_internal(const microjit::Ref<microjit::RectifiedFunction> &p_func) const {
    Ref<Assembly> assembly{};
    {
        std::lock_guard<std::mutex> guard(runtime->get_mutex());
        assembly = Ref<Assembly>::make_ref(runtime->get_asmjit_runtime());
    }
    auto& assembler = assembly->assembler;

    auto frame_report = create_frame_report(p_func);

    AINL("Prologue");
    AIN(assembler->push(rbp));
    AIN(assembler->mov(rbp, rsp));
    AIN(assembler->sub(rsp, frame_report->max_frame_size));

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
    auto branches_report = create_branches_report(assembler, p_func);
    const auto& offset_map = frame_report->variable_map;

    auto exit_label = assembler->newLabel();
    std::stack<ScopeInfo> scope_stack{};
    std::stack<Ref<BranchInfo>> loop_stack{};
    scope_stack.push(ScopeInfo{p_func->main_scope, -1, nullptr, nullptr, false });
    while (!scope_stack.empty()){
        auto current = scope_stack.top();
        AINL("Entering scope " << std::to_string((size_t)current.scope.ptr()));
        current.iterating++;
        scope_stack.pop();

        const auto& instructions = current.scope->get_instructions();
        for (auto s = int64_t(instructions.size()); current.iterating < s; current.iterating++){
            const auto& current_instruction = instructions[current.iterating];
            if (current.branch_info.is_valid() && !current.registered_begin) {
                AIN(assembler->bind(current.branch_info->begin_of_scope));
                current.registered_begin = true;
            }
            bool loop_break = false;
            switch (current_instruction->get_instruction_type()) {
                case Instruction::IT_CONSTRUCT: {
                    AINL("Constructing variable " << (size_t)current_instruction.ptr());
                    auto as_ctor = current_instruction.c_style_cast<ConstructInstruction>();
                    auto stack_offset = offset_map.at(as_ctor->target_variable);
                    AIN(assembler->lea(rdi, asmjit::x86::qword_ptr(rbp, stack_offset)));
                    AIN(assembler->call(as_ctor->ctor));
                    break;
                }
                case Instruction::IT_COPY_CONSTRUCT: {
                    auto as_cc = current_instruction.c_style_cast<CopyConstructInstruction>();
                    auto type = as_cc->target_variable->type;
                    AINL("Copy constructing variable " << (size_t)as_cc->target_variable.ptr());
                    auto stack_offset = offset_map.at(as_cc->target_variable);
                    switch (as_cc->value_reference->get_value_type()) {
                        case Value::VAL_IMMEDIATE: {
                            AIN(assembler->lea(rdi, asmjit::x86::qword_ptr(rbp, stack_offset)));
                            if (as_cc->target_variable->type.is_primitive)
                                copy_immediate_primitive(assembler, as_cc);
                            else copy_immediate(assembler, as_cc);
                            break;
                        }
                        case Value::VAL_ARGUMENT: {
                            auto as_arg = as_cc->value_reference.c_style_cast<ArgumentValue>();
                            auto arg_offset = function_arguments->argument_offsets()[as_arg->argument_index];
                            arg_offset += p_func->return_type.size;
                            copy_construct_variable_internal(assembler, type, as_cc->ctor,
                                                             { RelativeObject::STACK_BASE_PTR, stack_offset },
                                                             { RelativeObject::VIRTUAL_STACK_BASE_PTR, static_cast<int64_t>(arg_offset) });
                            break;
                        }
                        case Value::VAL_VARIABLE: {
                            auto as_var_val = as_cc->value_reference.c_style_cast<VariableValue>();
                            auto copy_target_offset = offset_map.at(as_var_val->variable);
                            copy_construct_variable_internal(assembler, type, as_cc->ctor,
                                                             { RelativeObject::STACK_BASE_PTR, stack_offset },
                                                             { RelativeObject::STACK_BASE_PTR, copy_target_offset });
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
                    auto stack_offset = offset_map.at(as_assign->target_variable);
                    auto type = as_assign->target_variable->type;
                    switch (as_assign->value_reference->get_value_type()) {
                        case Value::VAL_IMMEDIATE: {
                            AIN(assembler->lea(rdi, asmjit::x86::qword_ptr(rbp, stack_offset)));
                            if (type.is_primitive)
                                copy_immediate_primitive(assembler, as_assign);
                            else copy_immediate(assembler, as_assign);
                            break;
                        }
                        case Value::VAL_ARGUMENT: {
                            auto as_arg = as_assign->value_reference.c_style_cast<ArgumentValue>();
                            auto arg_offset = function_arguments->argument_offsets()[as_arg->argument_index];
                            arg_offset += p_func->return_type.size;
                            copy_construct_variable_internal(assembler, type, as_assign->ctor,
                                                             { RelativeObject::STACK_BASE_PTR, stack_offset },
                                                             { RelativeObject::VIRTUAL_STACK_BASE_PTR, static_cast<int64_t>(arg_offset) });
                            break;
                        }
                        case Value::VAL_VARIABLE: {
                            auto as_var_val = as_assign->value_reference.c_style_cast<VariableValue>();
                            auto copy_target_offset = offset_map.at(as_var_val->variable);
                            copy_construct_variable_internal(assembler, type, as_assign->ctor,
                                                             { RelativeObject::STACK_BASE_PTR, stack_offset },
                                                             { RelativeObject::STACK_BASE_PTR, copy_target_offset });
                            break;
                        }
                        case Value::VAL_EXPRESSION: {
                            assign_atomic_expression(assembler, frame_report, as_assign);
                            break;
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
                        auto stack_offset = offset_map.at(return_variable);
                        AIN(assembler->mov(rbx, LOAD_VRBP));
                        AIN(assembler->lea(rcx, asmjit::x86::qword_ptr(rbp, stack_offset)));
                        VAR_COPY(type.size, type.is_primitive, type.copy_constructor);
                    }

                    AINL("Destructing all stack items");
                    // Push the current frame so it can be destroyed
                    scope_stack.push(current);
                    iterative_destructor_call(assembler, frame_report, scope_stack);
                    scope_stack.pop();
                    AIN(assembler->jmp(exit_label));
                    // Skips every instructions left in this scope
                    loop_break = true;
                    current.iterating = -1;
                    // Bind the end of the scope
                    if (current.branch_info.is_valid())
                        AIN(assembler->bind(current.branch_info->end_of_scope));
                    // If this is a loop, remove it from the loop stack
                    if (!loop_stack.empty() && (loop_stack.top() == current.branch_info))
                        loop_stack.pop();
                    break;
                }
                case Instruction::IT_SCOPE_CREATE: {
                    AINL("Creating new scope");
                    scope_stack.push(current);
                    scope_stack.push(
                            ScopeInfo{ current_instruction.c_style_cast<ScopeCreateInstruction>()->scope,
                                       -1, nullptr, nullptr, false });
                    loop_break = true;
                    break;
                }
                case Instruction::IT_CONVERT: {
                    auto as_convert = current_instruction.c_style_cast<ConvertInstruction>();
                    AINL("Converting variable " << std::to_string((size_t)as_convert->from_var.ptr()) << " to variable " << std::to_string((size_t)as_convert->to_var.ptr()));
                    auto from_offset = offset_map.at(as_convert->from_var);
                    auto to_offset = offset_map.at(as_convert->to_var);
                    AIN(assembler->lea(rdi, asmjit::x86::qword_ptr(rbp, from_offset)));
                    AIN(assembler->lea(rsi, asmjit::x86::qword_ptr(rbp, to_offset)));
                    AIN(assembler->call(as_convert->converter));
                    break;
                }
                case Instruction::IT_PRIMITIVE_CONVERT: {
                    MJ_RAISE("Primitive conversion is currently unsupported");
//                    auto as_convert = current_instruction.c_style_cast<PrimitiveConvertInstruction>();
//                    auto from_offset = offset_map.at(as_convert->from_var);
//                    auto to_offset = offset_map.at(as_convert->to_var);
//                    auto converter_cb = converter.get_handler(as_convert->converter);
//                    converter_cb(assembler, LOAD_VRBP_LOC, from_offset, to_offset);
//                    break;
                }
                case Instruction::IT_INVOKE: {
                    auto as_invocation = current_instruction.c_style_cast<InvocationInstruction>();
                    AINL("Invoking function");
                    invoke_function(assembler, frame_report, p_func, as_invocation);
                    break;
                }
                case Instruction::IT_BRANCH: {
                    auto as_branch = current_instruction.c_style_cast<BranchInstruction>();
                    auto branch_info = branches_report->branch_map.at(as_branch);
                    switch (as_branch->branch_type) {
                        case BranchInstruction::BRANCH_IF: {
                            // Heh, as if
                            auto as_if = as_branch.c_style_cast<IfInstruction>();
                            if (!AbstractOperation::is_binary(as_if->condition->operation_type))
                                MJ_RAISE("Unary operations currently unsupported");
                            branch_eval_binary_atomic_expression(assembler, frame_report,
                                                                 branches_report, as_branch,
                                                                 branch_info,
                                                                 as_if->condition.c_style_cast<BinaryOperation>());
                            auto else_branch = branch_info->else_branch;
                            AIN(assembler->cmp(asmjit::x86::al, 0));
                            if (else_branch.is_valid()){
                                auto else_branch_report = branches_report->branch_map.at(else_branch);
                                AIN(assembler->je(else_branch_report->begin_of_scope));
                            } else {
                                AIN(assembler->je(branch_info->end_of_scope));
                            }
                            scope_stack.push(current);
                            scope_stack.push(
                                    ScopeInfo{ as_if->sub_scope,
                                               -1, as_branch, branch_info, false });
                            loop_break = true;
                            break;
                        }
                        case BranchInstruction::BRANCH_ELSE: {
                            auto as_else = as_branch.c_style_cast<ElseInstruction>();
                            scope_stack.push(current);
                            scope_stack.push(
                                    ScopeInfo{ as_else->sub_scope,
                                               -1, as_branch, branch_info, false });
                            loop_break = true;
                            break;
                        }
                        case BranchInstruction::BRANCH_WHILE: {
                            auto as_while = as_branch.c_style_cast<WhileInstruction>();
                            // Jump to the end to check conditions
                            AIN(assembler->jmp(branch_info->end_of_scope));
                            scope_stack.push(current);
                            scope_stack.push(
                                    ScopeInfo{ as_while->sub_scope,
                                               -1, as_branch, branch_info, false });
                            loop_stack.push(branch_info);
                            loop_break = true;
                            break;
                        }
                    }
                    break;
                }
                case Instruction::IT_BREAK: {
                    // If there's no loop, just do nothing
                    if (loop_stack.empty()) break;
                    single_scope_destructor_call(assembler, frame_report, current);
                    auto top_most_loop = loop_stack.top();
                    AIN(assembler->jmp(top_most_loop->loop_end_of_scope));
                    break;
                }
                case Instruction::IT_NONE:
                case Instruction::IT_DECLARE_VARIABLE:
                    break;
            }
            if (loop_break) break;
        }
        // If exited naturally
        if (current.iterating == current.scope->get_instructions().size()){
            // Currently at the last instruction,
            // call destructors
            single_scope_destructor_call(assembler, frame_report, current);
            if (current.branch_info.is_valid()) {
                AIN(assembler->bind(current.branch_info->end_of_scope));
                auto curr_branch_instruction = current.branch_instruction;
                switch (curr_branch_instruction->branch_type) {
                    case BranchInstruction::BRANCH_IF: {
                        auto else_scope = current.branch_info->else_branch;
                        if (else_scope.is_valid()) {
                            auto else_branch_info = branches_report->branch_map.at(else_scope);
                            // If condition have an else branch, jump to the end of it after exit normally
                            AIN(assembler->jmp(else_branch_info->end_of_scope));
                        }
                        break;
                    }
                    case BranchInstruction::BRANCH_WHILE: {
                        auto as_while = curr_branch_instruction.c_style_cast<WhileInstruction>();
                        if (!AbstractOperation::is_binary(as_while->condition->operation_type))
                            MJ_RAISE("Unary operations currently unsupported");
                        branch_eval_binary_atomic_expression(assembler, frame_report,
                                                             branches_report,
                                                             curr_branch_instruction,
                                                             current.branch_info,
                                                             as_while->condition.c_style_cast<BinaryOperation>());
                        // If satisfied, jump to the start of the scope
                        AIN(assembler->cmp(asmjit::x86::al, 0));
                        AIN(assembler->jne(current.branch_info->begin_of_scope));
                        AIN(assembler->bind(current.branch_info->loop_end_of_scope));
                        loop_stack.pop();
                        break;
                    }
                    default:
                        break;
                }
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
                                                                         RelativeObject p_receive_target,
                                                                         RelativeObject p_copy_target) {
    if (p_receive_target.unit == RelativeObject::VIRTUAL_STACK_BASE_PTR ||
        p_copy_target.unit == RelativeObject::VIRTUAL_STACK_BASE_PTR) {
        AIN(assembler->mov(asmjit::x86::r10, LOAD_VRBP));
    }
    if (p_receive_target.unit == RelativeObject::STACK_BASE_PTR) {
        AIN(assembler->lea(rbx, asmjit::x86::qword_ptr(rbp, p_receive_target.offset)));
    } else {
        AIN(assembler->mov(rbx, asmjit::x86::r10));
        if (p_receive_target.offset < 0)
            AIN(assembler->sub(rbx, std::abs(p_receive_target.offset)));
        else
            AIN(assembler->add(rbx, p_receive_target.offset));
    }

    if (p_copy_target.unit == RelativeObject::STACK_BASE_PTR) {
        AIN(assembler->lea(rcx, asmjit::x86::qword_ptr(rbp, p_copy_target.offset)));
    } else {
        AIN(assembler->mov(rcx, asmjit::x86::r10));
        if (p_copy_target.offset < 0)
            AIN(assembler->sub(rcx, std::abs(p_copy_target.offset)));
        else
            AIN(assembler->add(rcx, p_copy_target.offset));
    }
    VAR_COPY(p_type.size, p_type.is_primitive, p_copy_constructor);
}

//void microjit::MicroJITCompiler_x86_64::assign_variable_internal(microjit::Box<asmjit::x86::Assembler> &assembler,
//                                                                 const microjit::Type &p_type, const void *p_operator,
//                                                                 const int64_t &p_offset, const int64_t &p_copy_target) {
//    AIN(assembler->mov(rbx, LOAD_VRBP));
//    AIN(assembler->mov(rcx, rbx));
//    AIN(assembler->sub(rbx, std::abs(p_offset)));
//    if (p_copy_target >= 0) {
//        AIN(assembler->add(rcx, p_copy_target));
//    } else {
//        AIN(assembler->sub(rcx, std::abs(p_copy_target)));
//    }
//    VAR_COPY(p_type.size, p_type.is_primitive, p_operator);
//}

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
            auto stack_offset = p_frame_info->variable_map.at(var);
            AIN(assembler->mov(rdi, asmjit::x86::qword_ptr(rbp, stack_offset)));
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
        auto stack_offset = p_frame_info->variable_map.at(var);
        AIN(assembler->mov(rdi, asmjit::x86::qword_ptr(rbp, stack_offset)));
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

void microjit::MicroJITCompiler_x86_64::jit_trampoline_caller(JitFunctionTrampoline* p_trampoline,
                                                              VirtualStack *p_stack) {
    auto cb = &microjit::JitFunctionTrampoline::call_final;
    (p_trampoline->*cb)(p_stack);
}

void microjit::MicroJITCompiler_x86_64::native_trampoline_caller(microjit::BaseTrampoline *p_trampoline,
                                                                 microjit::VirtualStack *p_stack) {
    p_trampoline->call(p_stack);
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
    static TYPE_CONSTEXPR auto float_type = Type::create<float>();                                      \
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
                auto curr_stack_offset = p_frame_report->variable_map.at(curr_var);                     \
                AIN(assembler->lea(asmjit::x86::r10, asmjit::x86::qword_ptr(rbp, curr_stack_offset)));  \
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
            auto curr_stack_offset = p_frame_report->variable_map.at(curr_var);                         \
                AIN(assembler->lea(asmjit::x86::r10, asmjit::x86::qword_ptr(rbp, curr_stack_offset)));  \
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
        case microjit::AbstractOperation::BINARY_GREATER_OR_EQUAL:
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
        case microjit::AbstractOperation::BINARY_LESSER_OR_EQUAL:
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
        case microjit::AbstractOperation::BINARY_GREATER_OR_EQUAL: {
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
        case microjit::AbstractOperation::BINARY_LESSER_OR_EQUAL: {
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

    auto target_stack_offset = p_frame_report->variable_map.at(p_target_var);
    AIN(assembler->lea(PE_BUFFER, asmjit::x86::qword_ptr(rbp, target_stack_offset)));
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


void microjit::MicroJITCompiler_x86_64::branch_eval_binary_atomic_expression(
        microjit::Box<asmjit::x86::Assembler> &assembler,
        const microjit::Ref<microjit::MicroJITCompiler::StackFrameInfo> &p_frame_report,
        const microjit::Ref<microjit::MicroJITCompiler_x86_64::BranchesReport>& p_branches_report,
        const microjit::Ref<microjit::BranchInstruction> &p_target_var,
        const microjit::Ref<microjit::MicroJITCompiler_x86_64::BranchInfo> &p_branch_info,
        const microjit::Ref<microjit::BinaryOperation> &p_binary) {
    if (p_binary->is_primitive)
        branch_eval_primitive_binary_atomic_expression(assembler, p_frame_report, p_branches_report, p_target_var,
                                                       p_branch_info, p_binary.c_style_cast<PrimitiveBinaryOperation>());
    else
        MJ_RAISE("Branch evaluation only support primitive operations");
}

#define CLEAR_XMM(m_reg) AIN(assembler->xorps(REP_FP(m_reg), REP_FP(m_reg)))

void microjit::MicroJITCompiler_x86_64::branch_eval_primitive_binary_atomic_expression(
        microjit::Box<asmjit::x86::Assembler> &assembler,
        const microjit::Ref<microjit::MicroJITCompiler::StackFrameInfo> &p_frame_report,
        const microjit::Ref<microjit::MicroJITCompiler_x86_64::BranchesReport>& p_branches_report,
        const microjit::Ref<microjit::BranchInstruction> &p_instruction,
        const microjit::Ref<microjit::MicroJITCompiler_x86_64::BranchInfo> &p_branch_info,
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
    // Calculate the stuff
    if (AbstractOperation::operation_return_same(operation_type)){
        if (is_operand_floating_point) {
            floating_point_arithmetic(assembler, operation_type, operand_type.size);
            CLEAR_XMM(PRIMITIVE_EXPRESSION_RIGHT_FP);
            floating_point_comparison(assembler, AbstractOperation::BINARY_NOT_EQUAL, operand_type.size);
        } else {
            auto is_signed = Type::is_signed_integer(operand_type);
            integer_arithmetic(assembler, operation_type, operand_type);
        }
    } else if (AbstractOperation::operation_return_bool(operation_type)) {
        if (is_operand_floating_point) {
            floating_point_comparison(assembler, operation_type, operand_type.size);
        } else {
            integer_comparison(assembler, operation_type, operand_type);
        }
    } else
        MJ_RAISE("Unsupported operation");
}

void microjit::MicroJITCompiler_x86_64::invoke_function(microjit::Box<asmjit::x86::Assembler> &assembler,
                                                        const microjit::Ref<microjit::MicroJITCompiler::StackFrameInfo> &p_frame_report,
                                                        const Ref<RectifiedFunction>& p_func,
                                                        const microjit::Ref<microjit::InvocationInstruction> &p_instruction) {
    const auto target_trampoline = p_instruction->target_trampoline;
    const auto target_return_type = p_instruction->target_return_type;
    auto function_arguments = p_func->arguments;
    // Call's prologue
    // r11 now hold old vrbp
    AIN(assembler->mov(asmjit::x86::r11, LOAD_VRBP));
    AIN(assembler->mov(rdi, VSTACK_LOC));
    AIN(assembler->mov(rsi, target_return_type.size + p_instruction->arguments_total_size));
    AIN(assembler->call(virtual_stack_create_stack_frame));
    // Cache the new stack pointers
    AIN(assembler->mov(rdi, VSTACK_LOC));
    VSTACK_SETUP()
    // r10 now hold new vrbp
    AIN(assembler->mov(asmjit::x86::r10, LOAD_VRBP));
    for (const auto& arg : p_instruction->passed_arguments->values){
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
                AIN(assembler->mov(rbx, asmjit::x86::r10));
                // Load argument from Virtual stack
                AIN(assembler->mov(rcx, asmjit::x86::r11));
                AIN(assembler->add(rcx, arg_offset));
                VAR_COPY(type.size, type.is_primitive, type.copy_constructor);
                break;
            }
            case Value::VAL_VARIABLE: {
                auto as_var = arg.c_style_cast<VariableValue>();
                auto stack_offset = p_frame_report->variable_map.at(as_var->variable);
                const auto type = as_var->variable->type;
                AIN(assembler->sub(asmjit::x86::r10, type.size));
                AIN(assembler->mov(rbx, asmjit::x86::r10));
                AIN(assembler->lea(rcx, asmjit::x86::qword_ptr(rbp, stack_offset)));
                VAR_COPY(type.size, type.is_primitive, type.copy_constructor);
                break;
            }
            case Value::VAL_EXPRESSION:
                MJ_RAISE("Passing expression as argument is not supported. Evaluate them first.");
        }
    }
    AIN(assembler->sub(asmjit::x86::r10, target_return_type.size));
    // The stack setup process is finished
    // Load trampoline to rdi
    AIN(assembler->mov(rdi, (size_t)(target_trampoline.ptr())));
    // Load VirtualStack to rsi
    AIN(assembler->mov(rsi, VSTACK_LOC));
    // Call the trampoline
    if (p_instruction->is_jit) {
        // Note: I have to create a new stack frame again,
        // or it would not work with clang
        // I'm not going to add a macro here,
        // creating a new frame does not hurt the performance that much anyway
        AIN(assembler->push(rbp));
        AIN(assembler->mov(rbp, rsp));
        AIN(assembler->call(jit_trampoline_caller));
        AIN(assembler->leave());
    } else
        AIN(assembler->call(native_trampoline_caller));
    // Collapse the stack frame
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
    for (const auto& arg : p_instruction->passed_arguments->values) {
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
    const auto func_ret_type = target_return_type;
    if (func_ret_type.size > 0) {
        AIN(assembler->sub(asmjit::x86::r10, func_ret_type.size));
        // Copy the return value (if needed)
        auto return_var = p_instruction->return_variable;
        if (return_var.is_valid()){
            auto ret_var_offset = p_frame_report->variable_map.at(return_var);
            // Load variable address to rbx
            AIN(assembler->lea(rbx, asmjit::x86::qword_ptr(rbp, ret_var_offset)));
            AIN(assembler->mov(rcx, asmjit::x86::r10));
            // Copy return value into variable
            auto type = return_var->type;
            VAR_COPY(type.size, type.is_primitive, type.copy_constructor);
        }
        if (!func_ret_type.is_primitive) {
            // Cleanup the return value (if there's any)
            AIN(assembler->mov(rdi, asmjit::x86::r10));
            AIN(assembler->call(func_ret_type.destructor));
        }
    }
}


#endif

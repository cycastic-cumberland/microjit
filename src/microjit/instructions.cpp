//
// Created by cycastic on 8/17/23.
//

#include <stack>
#include "instructions.h"

microjit::Ref<microjit::ArgumentValue> microjit::ArgumentValue::create(const uint32_t &p_idx) {
    return Ref<ArgumentValue>::from_uninitialized_object(new ArgumentValue(p_idx));
}

microjit::Ref<microjit::CopyConstructInstruction>
microjit::CopyConstructInstruction::create_arg(const microjit::RectifiedScope *p_scope,
                                           const microjit::Ref<microjit::VariableInstruction> &p_target,
                                           const uint32_t &p_arg_idx) {
    const auto& types = p_scope->get_arguments()->argument_types();
    if (types.size() <= p_arg_idx) MJ_RAISE("Invalid argument index");
    if (types[p_arg_idx] != p_target->type) MJ_RAISE("Mismatched type");
    auto arg = ArgumentValue::create(p_arg_idx).c_style_cast<Value>();
    auto ins = new CopyConstructInstruction(p_target, arg, (const void*)p_target->type.copy_constructor);
    return Ref<CopyConstructInstruction>::from_uninitialized_object(ins);
}

microjit::Ref<microjit::CopyConstructInstruction>
microjit::CopyConstructInstruction::create_var(const microjit::Ref<microjit::VariableInstruction> &p_target,
                                               const microjit::Ref<microjit::VariableInstruction> &p_assign_target) {
    if (p_target->type != p_assign_target->type) MJ_RAISE("Mismatched type");
    auto var = VariableValue::create(p_assign_target).c_style_cast<Value>();
    auto ins = new CopyConstructInstruction(p_target, var, (const void*)p_target->type.copy_constructor);
    return Ref<CopyConstructInstruction>::from_uninitialized_object(ins);
}

microjit::Ref<microjit::VariableValue>
microjit::VariableValue::create(const microjit::Ref<microjit::VariableInstruction>& p_var){
    return Ref<VariableValue>::from_uninitialized_object(new VariableValue(p_var));
}

void microjit::RectifiedScope::push_instruction(Ref<Instruction> p_ins){
    p_ins->scope_offset = current_scope_offset++;
    instructions.push_back(p_ins);
}

microjit::Ref<microjit::InvokeJitInstruction>
microjit::RectifiedScope::invoke_jit(const microjit::Ref<microjit::RectifiedFunction> &p_func,
                                     const microjit::Ref<microjit::ArgumentsVector> &p_args,
                                     const Ref<VariableInstruction>& p_ret_var) {
    if (!p_func->trampoline) MJ_RAISE("p_func does not have a trampoline function. "
                                      "invoke_jit can only be called on functions that are created by the Orchestrator.");
    auto args = p_args.is_valid() ? p_args : Ref<ArgumentsVector>::make_ref();
    auto ins = InvokeJitInstruction::create(arguments, p_func, args, p_ret_var);
    push_instruction(ins.template c_style_cast<Instruction>());
    return ins;
}

microjit::RectifiedScope::RectifiedScope(const microjit::Ref<microjit::ArgumentsDeclaration> &p_args,
                                         const microjit::RectifiedScope *p_parent)
        : arguments(p_args), parent_scope(p_parent){}

bool
microjit::RectifiedScope::has_variable_in_all_scope(const microjit::Ref<microjit::VariableInstruction> &p_var) const {
    return has_variable(p_var) || (parent_scope && parent_scope->has_variable_in_all_scope(p_var));
}

microjit::Ref<microjit::CopyConstructInstruction>
microjit::RectifiedScope::construct_from_argument(const microjit::Ref<microjit::VariableInstruction> &p_var,
                                                  const uint32_t &p_idx) {
    if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
    auto ins = CopyConstructInstruction::create_arg(this, p_var, p_idx);
    push_instruction(ins.template c_style_cast<Instruction>());
    return ins;
}

microjit::Ref<microjit::CopyConstructInstruction>
microjit::RectifiedScope::construct_from_variable(const microjit::Ref<microjit::VariableInstruction> &p_var,
                                                  const microjit::Ref<microjit::VariableInstruction> &p_copy_target) {
    if (!has_variable(p_var)) MJ_RAISE("Does not own assignment target");
    if (!has_variable_in_all_scope(p_copy_target)) MJ_RAISE("Does not own copy target");
    if (p_var == p_copy_target) MJ_RAISE("Assign and copy target must not be the same");
    auto ins = CopyConstructInstruction::create_var(p_var, p_copy_target);
    push_instruction(ins.template c_style_cast<Instruction>());
    return ins;
}

microjit::Ref<microjit::ScopeCreateInstruction>
microjit::RectifiedScope::create_scope(const microjit::Ref<microjit::RectifiedScope> &p_child) {
    if (p_child->parent_scope != this) MJ_RAISE("Child's parent is not self");
    auto ins = Ref<ScopeCreateInstruction>::make_ref(p_child);
    directly_owned_scopes.push_back(p_child);
    push_instruction(ins.template c_style_cast<Instruction>());
    return ins;
}

microjit::Ref<microjit::PrimitiveAtomicBinaryExpressionParser>
microjit::RectifiedScope::create_primitive_binary_expression_parser() const {
    auto parser = new PrimitiveAtomicBinaryExpressionParser(this);
    return Ref<PrimitiveAtomicBinaryExpressionParser>::from_uninitialized_object(parser);
}

microjit::Ref<microjit::AssignInstruction> microjit::RectifiedScope::assign_from_primitive_atomic_expression(
        const microjit::Ref<microjit::VariableInstruction> &p_var,
        const microjit::AtomicBinaryExpressionParser::ParseResult &p_parse_result) {
    if (!has_variable_in_all_scope(p_var)) MJ_RAISE("Does not own assignment target");
    if (p_parse_result.host != this) MJ_RAISE("Does not own this expression");
    if (p_var->type != p_parse_result.return_type) MJ_RAISE("Mismatched return type");
    if (!p_parse_result.return_type.is_primitive) MJ_RAISE("Expression does not return a primitive");
    auto ins = AssignInstruction::create_expr_primitive(p_var, p_parse_result);
    push_instruction(ins.template c_style_cast<Instruction>());
    return ins;
}

microjit::Ref<microjit::CopyConstructInstruction> microjit::RectifiedScope::construct_from_primitive_atomic_expression(
        const microjit::Ref<microjit::VariableInstruction> &p_var,
        const microjit::AtomicBinaryExpressionParser::ParseResult &p_parse_result) {
    if (!has_variable_in_all_scope(p_var)) MJ_RAISE("Does not own assignment target");
    if (p_parse_result.host != this) MJ_RAISE("Does not own this expression");
    if (p_var->type != p_parse_result.return_type) MJ_RAISE("Mismatched return type");
    if (!p_parse_result.return_type.is_primitive) MJ_RAISE("Expression does not return a primitive");
    auto ins = CopyConstructInstruction::create_expr_primitive(p_var, p_parse_result);
    push_instruction(ins.template c_style_cast<Instruction>());
    return ins;
}

microjit::InvokeJitInstruction::InvokeJitInstruction(const microjit::Ref<microjit::RectifiedFunction> &p_func,
                                                     const microjit::Ref<microjit::ArgumentsVector> &p_args,
                                                     const Ref<VariableInstruction>& p_ret_var, const size_t& p_total_size)
        : Instruction(IT_INVOKE_JIT), target_function(p_func), passed_arguments(p_args),
          return_variable(p_ret_var), arguments_total_size(p_total_size) {}

microjit::Ref<microjit::InvokeJitInstruction>
microjit::InvokeJitInstruction::create(const Ref<ArgumentsDeclaration>& p_parent_args,
                                       const microjit::Ref<microjit::RectifiedFunction> &p_func,
                                       const microjit::Ref<microjit::ArgumentsVector> &p_args,
                                       const Ref<VariableInstruction>& p_ret_var){
    const auto& func_args = p_func->arguments;
    const auto& func_arg_types = func_args->argument_types();
    const auto& parent_arg_types = p_parent_args->argument_types();
    const auto& arg_values = p_args->values;
    size_t total_size = 0;
    // There can be no return variable, if you want to discard the return result
    if (p_ret_var.is_valid())
        if (p_ret_var->type != p_func->return_type)
            MJ_RAISE("Mismatched return type");
    if (parent_arg_types.size() != func_arg_types.size() ||
        func_arg_types.size() != arg_values.size())
        MJ_RAISE("Mismatched arguments size");
    for (size_t i = 0, s = func_arg_types.size(); i < s; i++){
        const auto& target_type = func_arg_types[i];
        const auto& argument_value = arg_values[i];
        switch (argument_value->get_value_type()) {
            case Value::VAL_IMMEDIATE: {
                auto as_imm = argument_value.c_style_cast<ImmediateValue>();
                if (as_imm->imm_type != target_type) MJ_RAISE("Mismatched argument type");
                total_size += as_imm->imm_type.size;
                break;
            }
            case Value::VAL_ARGUMENT: {
                auto as_arg = argument_value.c_style_cast<ArgumentValue>();
                auto model_type = parent_arg_types[as_arg->argument_index];
                if (model_type != target_type) MJ_RAISE("Mismatched argument type");
                total_size += model_type.size;
                break;
            }
            case Value::VAL_VARIABLE: {
                auto as_var = argument_value.c_style_cast<VariableValue>();
                auto var_type = as_var->variable->type;
                if (var_type != target_type) MJ_RAISE("Mismatched argument type");
                total_size += var_type.size;
                break;
            }
            case Value::VAL_EXPRESSION:
                MJ_RAISE("Passing expression as argument is not supported. Evaluate them first.");
        }
    }

    auto ins = new InvokeJitInstruction(p_func, p_args, p_ret_var, total_size);
    return Ref<InvokeJitInstruction>::from_uninitialized_object(ins);
}

microjit::Ref<microjit::PrimitiveBinaryOperation>
microjit::PrimitiveBinaryOperation::create(microjit::AbstractOperation::OperationType p_op,
                                           const microjit::Ref<microjit::Value>& p_left,
                                           const microjit::Ref<microjit::Value>& p_right) {
    auto ins = new PrimitiveBinaryOperation(p_op, p_left, p_right);
    return Ref<PrimitiveBinaryOperation>::from_uninitialized_object(ins);
}

microjit::Ref<microjit::VariableValue> microjit::VariableInstruction::value_reference() const {
    auto var_ins = Ref<VariableInstruction>::from_initialized_object((VariableInstruction*)this);
    auto val_ins = VariableValue::create(var_ins);
    return val_ins;
}

static _ALWAYS_INLINE_ void assert_type(microjit::Box<microjit::Type>& p_final_type, const microjit::Box<microjit::Type>& p_current_type){
    using namespace microjit;
    if (p_current_type.is_null()) MJ_RAISE("current_type is null");
    if (p_final_type.is_valid() && *p_current_type.ptr() != *p_final_type.ptr())
        MJ_RAISE("Mismatched expression type");
    else if (p_final_type.is_null())
        p_final_type = p_current_type;
}

#define BINARY_CHECK(m_op) if (!AbstractOperation::is_binary(m_op)) MJ_RAISE(#m_op " is not a binary operation")
#define PABEP_FINALIZE()                                                                                                \
    if (Type::is_floating_point(type) && p_op == AbstractOperation::BINARY_MOD)                                         \
        MJ_RAISE("Floating point number does not have modulo operator");                                                \
    Type expr_ret = (AbstractOperation::operation_return_same(p_op) ? type                                              \
    : (AbstractOperation::operation_return_bool(p_op) ? Type::create<bool>() : Type::create<void>()));                  \
    auto expr = PrimitiveBinaryOperation::create(p_op, p_left.c_style_cast<Value>(), p_right.c_style_cast<Value>());    \
    return create_result(expr.c_style_cast<AbstractOperation>(), expr_ret)

microjit::AtomicBinaryExpressionParser::ParseResult
microjit::PrimitiveAtomicBinaryExpressionParser::parse(microjit::AbstractOperation::OperationType p_op,
                                                       const microjit::Ref<microjit::ImmediateValue> &p_left,
                                                       const microjit::Ref<microjit::ImmediateValue> &p_right) {
    BINARY_CHECK(p_op);
    const auto type = p_left->imm_type;
    if (type != p_right->imm_type) MJ_RAISE("Mismatched type");
    if (!type.is_primitive) MJ_RAISE("Operands are not primitive");
    PABEP_FINALIZE();
}

microjit::AtomicBinaryExpressionParser::ParseResult
microjit::PrimitiveAtomicBinaryExpressionParser::parse(microjit::AbstractOperation::OperationType p_op,
                                                       const microjit::Ref<microjit::ImmediateValue> &p_left,
                                                       const microjit::Ref<microjit::VariableValue> &p_right) {
    BINARY_CHECK(p_op);
    if (!host_scope->has_variable_in_all_scope(p_right->variable))
        MJ_RAISE("Host scope does not own this variable");
    const auto type = p_left->imm_type;
    if (type != p_right->variable->type) MJ_RAISE("Mismatched type");
    if (!type.is_primitive) MJ_RAISE("Operands are not primitive");
    PABEP_FINALIZE();
}

microjit::AtomicBinaryExpressionParser::ParseResult
microjit::PrimitiveAtomicBinaryExpressionParser::parse(microjit::AbstractOperation::OperationType p_op,
                                                       const microjit::Ref<microjit::VariableValue> &p_left,
                                                       const microjit::Ref<microjit::ImmediateValue> &p_right) {
    BINARY_CHECK(p_op);
    if (!host_scope->has_variable_in_all_scope(p_left->variable))
        MJ_RAISE("Host scope does not own this variable");
    const auto type = p_left->variable->type;
    if (type != p_right->imm_type) MJ_RAISE("Mismatched type");
    if (!type.is_primitive) MJ_RAISE("Operands are not primitive");
    PABEP_FINALIZE();
}

microjit::AtomicBinaryExpressionParser::ParseResult
microjit::PrimitiveAtomicBinaryExpressionParser::parse(microjit::AbstractOperation::OperationType p_op,
                                                       const microjit::Ref<microjit::VariableValue> &p_left,
                                                       const microjit::Ref<microjit::VariableValue> &p_right) {
    BINARY_CHECK(p_op);
    if (!host_scope->has_variable_in_all_scope(p_left->variable) ||
        !host_scope->has_variable_in_all_scope(p_right->variable))
        MJ_RAISE("Host scope does not own this variable");
    const auto type = p_left->variable->type;
    if (type != p_right->variable->type) MJ_RAISE("Mismatched type");
    if (!type.is_primitive) MJ_RAISE("Operands are not primitive");
    PABEP_FINALIZE();
}

#undef PABEP_FINALIZE
#undef BINARY_CHECK

microjit::Ref<microjit::AssignInstruction> microjit::AssignInstruction::create_expr_primitive(const Ref<VariableInstruction> &p_target,
                                                                                              const microjit::AtomicBinaryExpressionParser::ParseResult &p_parse_result) {
    // Does not need to check if it's primitive, since this is called from RectifiedScope
    auto var = p_parse_result.expression.c_style_cast<Value>();
    auto ins = new AssignInstruction(p_target, var, (const void*)ObjectTools::empty_copy_ctor);
    return Ref<AssignInstruction>::from_uninitialized_object(ins);
}

microjit::Ref<microjit::CopyConstructInstruction> microjit::CopyConstructInstruction::create_expr_primitive(const Ref<VariableInstruction> &p_target,
                                                                                              const microjit::AtomicBinaryExpressionParser::ParseResult &p_parse_result) {
    // Does not need to check if it's primitive, since this is called from RectifiedScope
    auto var = p_parse_result.expression.c_style_cast<Value>();
    auto ins = new CopyConstructInstruction(p_target, var, (const void*)ObjectTools::empty_copy_ctor);
    return Ref<CopyConstructInstruction>::from_uninitialized_object(ins);
}

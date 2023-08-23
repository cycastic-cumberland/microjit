//
// Created by cycastic on 8/17/23.
//

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
                                     const microjit::Ref<microjit::ArgumentsVector> &p_args) {
    if (!p_func->trampoline) MJ_RAISE("p_func does not have a trampoline function. "
                                      "invoke_jit can only be called on functions that are created by the Orchestrator.");
    auto ins = InvokeJitInstruction::create(arguments, p_func, p_args);
    push_instruction(ins.template c_style_cast<Instruction>());
    return ins;
}

microjit::InvokeJitInstruction::InvokeJitInstruction(const microjit::Ref<microjit::RectifiedFunction> &p_func,
                                                     const microjit::Ref<microjit::ArgumentsVector> &p_args)
        : Instruction(IT_INVOKE_JIT), target_function(p_func), passed_arguments(p_args) {}

microjit::Ref<microjit::InvokeJitInstruction>
microjit::InvokeJitInstruction::create(const Ref<ArgumentsDeclaration>& p_parent_args,
                                       const microjit::Ref<microjit::RectifiedFunction> &p_func,
                                       const microjit::Ref<microjit::ArgumentsVector> &p_args){
    const auto& func_args = p_func->arguments;
    const auto& func_arg_types = func_args->argument_types();
    const auto& parent_arg_types = p_parent_args->argument_types();
    const auto& arg_values = p_args->values;
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
                break;
            }
            case Value::VAL_ARGUMENT: {
                auto as_arg = argument_value.c_style_cast<ArgumentValue>();
                auto model_type = parent_arg_types[as_arg->argument_index];
                if (model_type != target_type) MJ_RAISE("Mismatched argument type");
                break;
            }
            case Value::VAL_VARIABLE: {
                auto as_var = argument_value.c_style_cast<VariableValue>();
                if (as_var->variable->type != target_type) MJ_RAISE("Mismatched argument type");
                break;
            }
        }
    }

    auto ins = new InvokeJitInstruction(p_func, p_args);
    return Ref<InvokeJitInstruction>::from_uninitialized_object(ins);
}

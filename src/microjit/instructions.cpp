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

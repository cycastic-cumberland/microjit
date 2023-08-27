//
// Created by cycastic on 8/18/23.
//

#include "jit.h"
#include <stack>

#define MAX(m_a, m_b) ((m_a) > (m_b) ? (m_a) : (m_b))

microjit::Ref<microjit::MicroJITCompiler::StackFrameInfo>
microjit::MicroJITCompiler::create_frame_report(const microjit::Ref<microjit::RectifiedFunction> &p_func) {
    auto report = new StackFrameInfo();
    struct ScopeReport {
        Ref<RectifiedScope> scope;
        size_t current_size;
        size_t current_object_count;
        size_t iterating;
    };
    std::stack<ScopeReport> stack{};
    stack.push(ScopeReport{p_func->main_scope, 0, 0, 0});
    while (!stack.empty()){
        auto current = stack.top();
        stack.pop();
        const auto& instructions = current.scope->get_instructions();
        bool break_loop = false;
        for (; current.iterating < instructions.size(); current.iterating++){
            const auto& ins = instructions[current.iterating];
            switch (ins->get_instruction_type()) {
                case Instruction::IT_DECLARE_VARIABLE: {
                    auto as_var = ins.c_style_cast<VariableInstruction>();
                    current.current_size += as_var->type.size;
                    current.current_object_count++;
                    report->max_frame_size = MAX(report->max_frame_size, current.current_size);
                    report->max_object_allocation = MAX(report->max_object_allocation, current.current_object_count);
                    report->variable_map[as_var] = -int64_t(current.current_size);
                    break;
                }
                case Instruction::IT_SCOPE_CREATE:
                    current.iterating++;
                    stack.push(current);
                    stack.push(ScopeReport{ins.c_style_cast<ScopeCreateInstruction>()->scope,
                                              current.current_size,
                                              current.current_object_count,
                                              0});
                    break_loop = true;
                    break;
                case Instruction::IT_BRANCH:
                    current.iterating++;
                    stack.push(current);
                    stack.push(ScopeReport{ins.c_style_cast<BranchInstruction>()->sub_scope,
                                           current.current_size,
                                           current.current_object_count,
                                           0});
                    break_loop = true;
                    break;
                default:
                    break;
            }
            if (break_loop) break;
        }
    }
    return microjit::Ref<microjit::MicroJITCompiler::StackFrameInfo>::from_uninitialized_object(report);
}

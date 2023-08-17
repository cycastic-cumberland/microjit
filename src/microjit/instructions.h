//
// Created by cycastic on 8/15/23.
//

#ifndef MICROJIT_INSTRUCTIONS_H
#define MICROJIT_INSTRUCTIONS_H

#include <typeinfo>
#include <typeindex>
#include <utility>
#include <vector>
#include <functional>
#include <list>
#include <unordered_map>
#include <string>
#include <cstring>

#include "helper.h"
#include "virtual_stack.h"
#include "utils.h"

namespace microjit {
    class ObjectTools {
    public:
        template<class T>
        static void ctor(T* p_obj) { new (p_obj) T(); }
        template<class T>
        static void copy_ctor(T* p_obj, const T* p_copy_target) { new (p_obj) T(*p_copy_target); }
        static void str_copy_constructor(char* p_obj, const char* p_copy_target) {
            size_t i = 0;
            while (auto c = (p_copy_target)[i++]){
                (p_obj)[i - 1] = c;
            }
            (p_obj)[i - 1] = 0;
        }
        template<class T>
        static void dtor(T* p_obj) { p_obj->~T(); }

        template<class T>
        static void assign(T* p_obj, const T* copy_target) { *p_obj = *p_obj; }

        static void empty_ctor(void*) {  }
        static void empty_copy_ctor(void*, void*) {  }
        static void empty_dtor(void*) {  }
    };
    struct Type {
        const std::type_index type_index;
        const size_t size;
        const void* copy_constructor;
        const void* destructor;
        const bool is_fundamental;
    private:
        Type(const std::type_index& p_idx,
             const size_t& p_size,
             const void* p_cc,
             const void* p_dtor,
             const bool& p_fundamental)
            : type_index(p_idx), size(p_size),
            copy_constructor(p_cc), destructor(p_dtor), is_fundamental(p_fundamental) {}

        static Type create_internal(void (*)()){
            static const auto copy_ctor = (const void*)ObjectTools::empty_copy_ctor;
            static const auto dtor = (const void*)ObjectTools::empty_dtor;
            return { typeid(void), 0, copy_ctor, dtor, true };
        }

        template<typename T>
        static Type create_internal(T (*)()){
            static const auto is_trivially_destructible  = std::is_trivially_destructible<T>::value;

            static const auto copy_ctor = (const void*)ObjectTools::copy_ctor<T>;
            static const auto dtor = (is_trivially_destructible ?
                                (const void*)ObjectTools::empty_dtor :
                                (const void*)ObjectTools::dtor<T>);
            static const auto fundamental = std::is_fundamental<T>::value;
            return { typeid(T), sizeof(T), copy_ctor, dtor, fundamental };
        }

    public:
        _NO_DISCARD_ bool operator==(const Type& p_right) const {
            return type_index == p_right.type_index;
        }
        _NO_DISCARD_ bool operator!=(const Type& p_right) const {
            return type_index != p_right.type_index;
        }

        template<typename T>
        static Type create(){
            // A little cheat for void type
            static constexpr T (*f)() = nullptr;
            return create_internal(f);
        }
        static Type create_string_literal(const size_t& p_str_len){
            static const auto copy_ctor = (const void*)ObjectTools::str_copy_constructor;
            static const auto dtor = (const void*)ObjectTools::empty_dtor;
            return { typeid(const char*), p_str_len + 1, copy_ctor, dtor, true };
        }
    };
    template <typename R, typename ...Args> class Function;
    template <typename R, typename ...Args> class Scope;

    class RectifiedScope;

    class Instruction : public ThreadUnsafeObject{
    public:
        enum InstructionType {
            IT_NONE,
            IT_DECLARE_VARIABLE,
            IT_CONSTRUCT,
            IT_COPY_CONSTRUCT,
            IT_ASSIGN,
            IT_RETURN,
        };
    private:
        const InstructionType type;
    protected:
        explicit Instruction(const InstructionType& p_type) : type(p_type) {}
    public:
        InstructionType get_instruction_type() const { return type; }
    };

    class Value : public ThreadUnsafeObject {
    public:
        enum ValueType {
            VAL_IMMEDIATE,
            VAL_ARGUMENT,
        };
    private:
//        const Instruction* host{};
        const ValueType type;

//        friend class RectifiedScope;
    protected:
        explicit Value(const ValueType& p_type) : type(p_type) {}
    public:
        ValueType get_value_type() const { return type; }
    };

    class ImmediateValue : public Value {
    public:
        void* data;
        const Type imm_type;
    private:
        ImmediateValue(void* p_data, Type&& p_type) : Value(VAL_IMMEDIATE), data(p_data), imm_type(p_type) {}
    public:
        ~ImmediateValue() override {
            auto dtor = (void (*)(void*))imm_type.destructor;
            dtor(data);
            free(data);
        }
        template<typename T>
        static Ref<ImmediateValue> create(const T& p_value){
            auto type = Type::create<T>();
            auto data = malloc(type.size);
            auto cc = (void (*)(void*, const void*))type.copy_constructor;
            cc(data, &p_value);
            auto ins = new ImmediateValue(data, std::move(type));
            return Ref<ImmediateValue>::from_uninitialized_object(ins);
        }
    };

    class ArgumentValue : public Value {
    public:
        const uint32_t& argument_index;
    private:
        explicit ArgumentValue(const uint32_t& p_idx) : Value(VAL_ARGUMENT), argument_index(p_idx) {}
    public:
        static Ref<ArgumentValue> create(const uint32_t& p_idx);
    };

    class ArgumentsDeclaration : public ThreadUnsafeObject {
    private:
        std::vector<Type> arg_types{};
        std::vector<size_t> offsets{};
    private:
        template<typename T>
        _ALWAYS_INLINE_ void add_arg(){
            arg_types.push_back(Type::create<T>());
        }
        void create_indexes(){
            offsets.resize(arg_types.size());
            size_t current_offset = 0;
            // The stack growth downward
            for (int64_t i = offsets.size() - 1; i >= 0; i--){
                offsets[i] = current_offset;
                current_offset += arg_types[i].size;
            }
        }
        ArgumentsDeclaration() = default;
    public:
        const std::vector<Type>& argument_types() const { return arg_types; }
        const std::vector<size_t>& argument_offsets() const { return offsets; }

        template<typename ...Args>
        static Ref<ArgumentsDeclaration> create(){
            auto args = new ArgumentsDeclaration();
            (args->add_arg<Args>(), ...);
            args->create_indexes();
            return Ref<ArgumentsDeclaration>::from_uninitialized_object(args);
        }
    };

    class VariableInstruction : public Instruction {
    public:
        enum Property : uint32_t {
            NONE = 0,
            CONST = 1,
        };
        const uint32_t local_id;
        const RectifiedScope* parent_scope;
        const Type type;
        const uint32_t properties;
    private:
        VariableInstruction(const uint32_t& p_local_id, const RectifiedScope* p_scope, Type p_type, const uint32_t& p_props)
            : Instruction(IT_DECLARE_VARIABLE),
              local_id(p_local_id), parent_scope(p_scope), type(std::move(p_type)), properties(p_props) {}
    public:
        template<class T>
        static Ref<VariableInstruction> create(const uint32_t& p_local_id, const RectifiedScope* p_scope, const uint32_t& p_props){
            return Ref<VariableInstruction>::from_uninitialized_object(
                    new VariableInstruction(p_local_id, p_scope, Type::create<T>(), p_props));
        }
        static Ref<VariableInstruction> create_string_literal(const uint32_t& p_local_id,
                                                              const RectifiedScope* p_scope,
                                                              const size_t& p_str_len){
            return Ref<VariableInstruction>::from_uninitialized_object(
                    new VariableInstruction(p_local_id, p_scope, Type::create_string_literal(p_str_len), Property::CONST));
        }
        
    };
    class ConstructInstruction : public Instruction {
    public:
        const Ref<VariableInstruction> target_variable;
        const void* ctor;
    private:
        ConstructInstruction(const Ref<VariableInstruction>& p_target, const void* p_ctor)
            : Instruction(IT_CONSTRUCT), target_variable(p_target), ctor(p_ctor){}
    public:
        template<typename T>
        static Ref<ConstructInstruction> create_unsafe(const Ref<VariableInstruction>& p_target){
            // Does not check for type, just need to have a default constructor
            auto ins = new ConstructInstruction(p_target, (const void*)ObjectTools::ctor<T>);
            return Ref<ConstructInstruction>::from_uninitialized_object(ins);
        }
        template<typename T>
        static Ref<ConstructInstruction> create(const Ref<VariableInstruction>& p_target) {
            auto type = Type::create<T>();
            if (type != p_target->type) MJ_RAISE("Mismatched type");
            return create_unsafe<T>(p_target);
        }
    };
    class CopyConstructInstruction : public Instruction {
    public:
        const Ref<VariableInstruction> target_variable;
        const Ref<Value> value_reference;
        void const* ctor; // void (*)(void*, const void*)
    private:
        CopyConstructInstruction(const Ref<VariableInstruction>& p_target, const Ref<Value>& p_val, void const* p_ctor)
            : Instruction(IT_COPY_CONSTRUCT), target_variable(p_target), value_reference(p_val), ctor(p_ctor){}
    public:
        // This method don't have an unsafe equivalent as it's constructor is saved in its Type data
        template<typename T>
        static Ref<CopyConstructInstruction> create_imm(const Ref<VariableInstruction>& p_target, const T& p_value){
            auto imm = ImmediateValue::create(p_value);
            if (imm->imm_type != p_target->type) MJ_RAISE("Mismatched type");
            auto as_value = imm.template c_style_cast<Value>();
            auto ins = new CopyConstructInstruction(p_target, as_value, (const void*)p_target->type.copy_constructor);
            return Ref<CopyConstructInstruction>::from_uninitialized_object(ins);
        }
        static Ref<CopyConstructInstruction> create_arg(const RectifiedScope* p_scope, const Ref<VariableInstruction>& p_target, const uint32_t& p_arg_idx);
    };
    class AssignInstruction : public Instruction {
    public:
        const Ref<VariableInstruction> target_variable;
        const Ref<Value> value_reference;
        void const* ctor; // void (*)(void*, const void*)
    private:
        AssignInstruction(const Ref<VariableInstruction>& p_target, const Ref<Value>& p_val, void const* p_ctor)
                : Instruction(IT_ASSIGN), target_variable(p_target), value_reference(p_val), ctor(p_ctor){}
    public:
        template<typename T>
        static Ref<AssignInstruction> create_imm_unsafe(const Ref<VariableInstruction>& p_target, const T& p_value) {
            auto imm = ImmediateValue::create(p_value).template c_style_cast<Value>();
            auto ins = new AssignInstruction(p_target, imm, ObjectTools::assign<T>);
            return Ref<AssignInstruction>::from_uninitialized_object(ins);
        }
        template<typename T>
        static Ref<AssignInstruction> create_imm(const Ref<VariableInstruction>& p_target, const T& p_value) {
            if (Type::create<T>() != p_target->type) MJ_RAISE("Mismatched type");
            return create_imm_unsafe(p_target, p_value);
        }
        template<typename T>
        static Ref<AssignInstruction> create_arg_unsafe(const RectifiedScope* p_scope, const Ref<VariableInstruction>& p_target, const uint32_t& p_idx);
        template<typename T>
        static Ref<AssignInstruction> create_arg(const RectifiedScope* p_scope, const Ref<VariableInstruction>& p_target, const uint32_t& p_idx){
            if (Type::create<T>() != p_target->type) MJ_RAISE("Mismatched type");
            return create_arg_unsafe<T>(p_target, p_idx);
        }
    };

    class ReturnInstruction : public Instruction {
    public:
        // null for void functions
        const Ref<VariableInstruction> return_var;

        explicit ReturnInstruction(const Ref<VariableInstruction>& p_ret) : Instruction(IT_RETURN), return_var(p_ret) {}
    };

    class RectifiedScope : public ThreadUnsafeObject {
    private:
        const RectifiedScope* parent_scope;
        const Ref<ArgumentsDeclaration> arguments;
        std::vector<Ref<VariableInstruction>> variables{};
        std::vector<Ref<Instruction>> instructions{};
        uint32_t current_local_id{};
    public:
        explicit RectifiedScope(const Ref<ArgumentsDeclaration>& p_args, const RectifiedScope* p_parent = nullptr)
            : arguments(p_args), parent_scope(p_parent){}
        _NO_DISCARD_ const auto& get_arguments() const { return arguments; }
        _NO_DISCARD_ bool has_variable(const Ref<VariableInstruction>& p_var) const {
            return (p_var.is_valid() && p_var->parent_scope == this && p_var->local_id <= current_local_id);
        }
        _NO_DISCARD_ bool has_variable_in_all_scope(const Ref<VariableInstruction>& p_var) const {
            return has_variable(p_var) || (parent_scope && parent_scope->has_variable_in_all_scope(p_var));
        }
        template<typename T>
        Ref<VariableInstruction> create_variable(const uint32_t& p_props = VariableInstruction::NONE){
            // Create an anonymous ins
            auto ins = VariableInstruction::create<T>(++current_local_id, this, p_props);
            variables.push_back(ins);
            instructions.push_back(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<ConstructInstruction> default_construct_unsafe(const Ref<VariableInstruction>& p_var){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = ConstructInstruction::create_unsafe<T>(p_var);
            instructions.push_back(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<ConstructInstruction> default_construct(const Ref<VariableInstruction>& p_var){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = ConstructInstruction::create<T>(p_var);
            instructions.push_back(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<CopyConstructInstruction> construct_from_immediate(const Ref<VariableInstruction>& p_var, const T& p_imm){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = CopyConstructInstruction::create_imm(p_var, p_imm);
            instructions.push_back(ins.template c_style_cast<Instruction>());
            return ins;
        }
        Ref<CopyConstructInstruction> construct_from_argument(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = CopyConstructInstruction::create_arg(this, p_var, p_idx);
            instructions.push_back(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_immediate_unsafe(const Ref<VariableInstruction>& p_var, const T& p_imm){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = AssignInstruction::create_imm_unsafe(p_var, p_imm);
            instructions.push_back(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_immediate(const Ref<VariableInstruction>& p_var, const T& p_imm){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = AssignInstruction::create_imm(p_var, p_imm);
            instructions.push_back(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_argument_unsafe(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = AssignInstruction::create_arg_unsafe<T>(this, p_var, p_idx);
            instructions.push_back(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_argument(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = AssignInstruction::create_arg<T>(this, p_var, p_idx);
            instructions.push_back(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename R>
        Ref<ReturnInstruction> function_return(const Ref<VariableInstruction>& p_var = Ref<VariableInstruction>::null()){
            // Does not own this variable;
            if (p_var.is_null() || !has_variable(p_var)) MJ_RAISE("Does not own variable");
            // Does not match the return type
            static const auto return_type = Type::create<R>();
            // If return type is void, then p_var must be null
            if (return_type.size != 0) {
                if (p_var->type != return_type)
                    MJ_RAISE("Return type mismatched");
            } else if (p_var.is_valid()) MJ_RAISE("p_var must be invalid for functions that return 'void'");
            auto return_instruction = Ref<ReturnInstruction>::make_ref(p_var);
            instructions.push_back(return_instruction.template c_style_cast<Instruction>());
            return return_instruction;
        }

        _NO_DISCARD_ const auto& get_instructions() const { return instructions; }
        _NO_DISCARD_ const auto& get_variables() const { return variables; }
    };

    template <typename R, typename ...Args>
    class Scope : public ThreadUnsafeObject {
    private:
        Function<R, Args...>* parent_function;
        const Scope<R, Args...>* parent_scope;
        Ref<RectifiedScope> rectified_scope;
    public:
        explicit Scope(Function<R, Args...>* p_parent, const Scope<R, Args...>* p_parent_scope = nullptr)
                : parent_function(p_parent), parent_scope(p_parent_scope),
                  rectified_scope(Ref<RectifiedScope>::make_ref(p_parent->get_arguments_data(),
                                                                parent_scope ? parent_scope->rectified_scope.ptr() : nullptr)) {}

        Ref<Scope<R, Args...>> create_scope() {
            return Ref<Scope<R, Args...>>::make_ref(parent_scope, this);
        }

        bool has_variable(const Ref<VariableInstruction>& p_var) const {
            return rectified_scope->has_variable(p_var);
        }
        bool has_variable_in_all_scope(const Ref<VariableInstruction>& p_var) const {
            return rectified_scope->has_variable_in_all_scope(p_var);
        }
        template<typename T>
        Ref<VariableInstruction> create_variable(const uint32_t& p_props = VariableInstruction::NONE){
            return rectified_scope->template create_variable<T>(p_props);
        }
        template<typename T>
        Ref<ConstructInstruction> default_construct_unsafe(const Ref<VariableInstruction>& p_var){
            return rectified_scope->default_construct_unsafe<T>(p_var);
        }
        template<typename T>
        Ref<ConstructInstruction> default_construct(const Ref<VariableInstruction>& p_var){
            return rectified_scope->default_construct<T>(p_var);
        }
        template<typename T>
        Ref<CopyConstructInstruction> construct_from_immediate(const Ref<VariableInstruction>& p_var, const T& p_imm){
            return rectified_scope->construct_from_immediate<T>(p_var, p_imm);
        }
        Ref<CopyConstructInstruction> construct_from_argument(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            return rectified_scope->construct_from_argument(p_var, p_idx);
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_immediate_unsafe(const Ref<VariableInstruction>& p_var, const T& p_imm){
            return rectified_scope->assign_from_immediate_unsafe<T>(p_var, p_imm);
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_immediate(const Ref<VariableInstruction>& p_var, const T& p_imm){
            return rectified_scope->assign_from_immediate<T>(p_var, p_imm);
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_argument_unsafe(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            return rectified_scope->assign_from_argument_unsafe<T>(p_var, p_idx);
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_argument(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            return rectified_scope->assign_from_argument<T>(p_var, p_idx);
        }
        Ref<ReturnInstruction> function_return(const Ref<VariableInstruction>& p_var = Ref<VariableInstruction>::null()){
            return rectified_scope->function_return<R>(p_var);
        }
        _NO_DISCARD_ Ref<RectifiedScope> get_rectified_scope() const { return rectified_scope; }
    };

    class RectifiedFunction : public ThreadUnsafeObject {
    public:
        const void* host;
        const Ref<ArgumentsDeclaration> arguments;
        const Ref<RectifiedScope> main_scope;
        const Type return_type;
    private:
        RectifiedFunction(const void* p_host,
                          const Ref<ArgumentsDeclaration>& p_args,
                          const Ref<RectifiedScope>& p_main_scope,
                          Type p_ret)
                          : host(p_host), arguments(p_args), main_scope(p_main_scope), return_type(std::move(p_ret)) {}
    public:
        template <typename R, typename ...Args>
        static Ref<RectifiedFunction> create(const Function<R, Args...>* p_host,
                                             const Ref<ArgumentsDeclaration>& p_args,
                                             const Ref<RectifiedScope>& p_main_scope,
                                             const Type& p_ret){
            auto rectified = new RectifiedFunction(p_host, p_args, p_main_scope, p_ret);
            return Ref<RectifiedFunction>::from_uninitialized_object(rectified);
        }
    };

    template <typename R, typename ...Args>
    class Function : public ThreadUnsafeObject {
    private:
        const Ref<ArgumentsDeclaration> arguments;
        Ref<Scope<R, Args...>> main_scope;

    public:
        Function() : arguments(ArgumentsDeclaration::create<Args...>()),
                     main_scope(Ref<Scope<R, Args...>>::make_ref(this)) {}

        Ref<Scope<R, Args...>> get_main_scope() { return main_scope; }
        const Ref<ArgumentsDeclaration>& get_arguments_data() const { return arguments; }
        Ref<RectifiedFunction> rectify() const {
            return RectifiedFunction::create(this, arguments, main_scope->get_rectified_scope(), Type::create<R>());
        }
    };
    template<typename T>
    Ref<AssignInstruction>
    AssignInstruction::create_arg_unsafe(const RectifiedScope *p_scope, const Ref<VariableInstruction> &p_target,
                                         const uint32_t &p_idx) {
        const auto& types = p_scope->get_arguments()->argument_types();
        if (types.size() <= p_idx) MJ_RAISE("Invalid argument index");
        if (types[p_idx] != p_target->type) MJ_RAISE("Mismatched type");
        auto arg = ArgumentValue::create(p_idx).c_style_cast<Value>();
        auto ins = new AssignInstruction(p_target, arg, ObjectTools::assign<T>);
        return Ref<AssignInstruction>::from_uninitialized_object(ins);
    }
}

#endif //MICROJIT_INSTRUCTIONS_H

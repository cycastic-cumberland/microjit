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
        const void* constructor;
        const void* copy_constructor;
        const void* destructor;
    private:
        Type(const std::type_index& p_idx, const size_t& p_size, const void* p_ctor, const void* p_cc, const void* p_dtor)
            : type_index(p_idx), size(p_size), constructor(p_ctor), copy_constructor(p_cc), destructor(p_dtor) {}

        static Type create_internal(void (*)()){
            static const auto ctor = (const void*)ObjectTools::empty_ctor;
            static const auto copy_ctor = (const void*)ObjectTools::empty_copy_ctor;
            static const auto dtor = (const void*)ObjectTools::empty_dtor;
            return { typeid(void), 0, ctor, copy_ctor, dtor };
        }

        template<typename T>
        static Type create_internal(T (*)()){
            static const auto is_trivially_constructable = std::is_trivially_constructible<T>::value;
//            static const auto is_trivially_copy_constructable = std::is_trivially_copy_constructible<T>::value;
            static const auto is_trivially_destructible  = std::is_trivially_destructible<T>::value;
            static const auto ctor = (is_trivially_constructable ?
                                (const void*)ObjectTools::empty_ctor :
                                (const void*)ObjectTools::ctor<T>);
            static const auto copy_ctor = (const void*)ObjectTools::copy_ctor<T>;
            static const auto dtor = (is_trivially_destructible ?
                                (const void*)ObjectTools::empty_dtor :
                                (const void*)ObjectTools::dtor<T>);
            return { typeid(T), sizeof(T), ctor, copy_ctor, dtor };
        }

    public:
        template<typename T>
        static Type create(){
            // A little cheat for void type
            static constexpr T (*f)() = nullptr;
            return create_internal(f);
        }
        static Type create_string_literal(const size_t& p_str_len){
            static const auto ctor = (const void*)ObjectTools::empty_ctor;
            static const auto copy_ctor = (const void*)ObjectTools::str_copy_constructor;
            static const auto dtor = (const void*)ObjectTools::empty_dtor;
            return { typeid(const char*), p_str_len + 1, ctor, copy_ctor, dtor };
        }
    };
    template <typename R, typename ...Args> class Function;
    template <typename R, typename ...Args> class Scope;

    class Instruction : public ThreadUnsafeObject{
    public:
        enum InstructionType {
            IT_NONE,
            IT_DECLARE_VARIABLE,
            IT_RETURN,
            IT_CONSTRUCT_IMM,
        };
    private:
        const InstructionType type;
    protected:
        explicit Instruction(const InstructionType& p_type) : type(p_type) {}
    public:
        InstructionType get_instruction_type() const { return type; }
    };

    class RectifiedScope;

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
    class ReturnInstruction : public Instruction {
    public:
        // null for void functions
        const Ref<VariableInstruction> return_var;

        explicit ReturnInstruction(const Ref<VariableInstruction>& p_ret) : Instruction(IT_RETURN), return_var(p_ret) {}
    };
    class CopyConstructImmInstruction : public Instruction {
    public:
        const Ref<VariableInstruction> assign_target;
        const Type type;
        void* data;
    private:
        CopyConstructImmInstruction(const Ref<VariableInstruction>& p_target, Type&& p_type, void* p_data)
            : Instruction(IT_CONSTRUCT_IMM), assign_target(p_target), type(p_type), data(p_data) {}

        static Ref<CopyConstructImmInstruction> create_internal(const Ref<VariableInstruction>& p_target, const char* const& imm){
            const auto p_len = strlen(imm);
            auto partition = malloc(p_len + 1);
            auto type = Type::create_string_literal(p_len);
            const auto cc = type.copy_constructor;
            ((void (*)(char*, const char*))cc)((char*)partition, imm);
            auto re = new CopyConstructImmInstruction(p_target, std::move(type), partition);
            return Ref<CopyConstructImmInstruction>::from_uninitialized_object(re);
        }
        template<typename T>
        static Ref<CopyConstructImmInstruction> create_internal(const Ref<VariableInstruction>& p_target, const T& imm){
            auto type = Type::create<T>();
            if (p_target->type.type_index != type.type_index) MJ_RAISE("Mismatched return type");
            // Now allow anything as long as it has a copy constructor
            if (!std::is_copy_constructible<T>::value) MJ_RAISE("Return value is not copy constructable");
            auto partition = malloc(type.size);
            auto cc = (void (*)(T*, const T*))type.copy_constructor;
            cc((T*)partition, &imm);
            auto re = new CopyConstructImmInstruction(p_target, std::move(type), partition);
            return Ref<CopyConstructImmInstruction>::from_uninitialized_object(re);
        }
    public:
        ~CopyConstructImmInstruction() override {
            ((void (*)(void*))(type.destructor))(data);
            free(data);
        }

        // Assuming that the assignment callback is copy construction
        template<typename T>
        static Ref<CopyConstructImmInstruction> create(const Ref<VariableInstruction>& p_target, const T& imm){
            return create_internal(p_target, imm);
        }
    };

    class RectifiedScope : public ThreadUnsafeObject {
    private:
        const RectifiedScope* parent_scope;
        std::vector<Ref<VariableInstruction>> variables{};
        std::vector<Ref<Instruction>> instructions{};
        uint32_t current_local_id{};
    public:
        explicit RectifiedScope(const RectifiedScope* p_parent = nullptr) : parent_scope(p_parent){}
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
        template<typename R>
        Ref<ReturnInstruction> function_return(const Ref<VariableInstruction>& p_var = Ref<VariableInstruction>::null()){
            // Does not own this variable;
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            // Does not match the return type
            static const auto return_type = Type::create<R>();
            // If return type is void, then p_var must be null
            if (return_type.size != 0) {
                if (p_var->type.type_index != return_type.type_index) MJ_RAISE("Return type mismatched");
            } else if (p_var.is_valid()) MJ_RAISE("p_var must be invalid for functions that return 'void'");
            auto return_instruction = Ref<ReturnInstruction>::make_ref(p_var);
            instructions.push_back(return_instruction.template c_style_cast<Instruction>());
            return return_instruction;
        }

        template<typename T>
        Ref<CopyConstructImmInstruction> construct_immediate(const Ref<VariableInstruction>& p_var, const T& p_imm){
            if (!has_variable_in_all_scope(p_var)) return Ref<CopyConstructImmInstruction>::null();
            auto ins = CopyConstructImmInstruction::create(p_var, p_imm);
            instructions.push_back(ins.template c_style_cast<Instruction>());
            return ins;
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
                  rectified_scope(Ref<RectifiedScope>::make_ref(parent_scope ? parent_scope->rectified_scope.ptr() : nullptr)) {}

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
//        Ref<VariableInstruction> create_string_literal(const size_t& p_str_len) {
//            return rectified_scope->create_string_literal(p_str_len);
//        }
        Ref<ReturnInstruction> function_return(const Ref<VariableInstruction>& p_var = Ref<VariableInstruction>::null()){
            return rectified_scope->function_return<R>(p_var);
        }
        template<typename T>
        Ref<CopyConstructImmInstruction> construct_immediate(const Ref<VariableInstruction>& p_var, const T& p_imm){
            return rectified_scope->construct_immediate(p_var, p_imm);
        }
        _NO_DISCARD_ Ref<RectifiedScope> get_rectified_scope() const { return rectified_scope; }
    };

//    template <typename ...Args>
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
    class FunctionCreator {
    public:
        template<typename R, typename ...Args>
        static Ref<Function<R, Args...>> from_model(const std::function<R(Args...)>&){
            return Ref<Function<R, Args...>>::make_ref();
        }
    };
}

#undef MJ_RAISE

#endif //MICROJIT_INSTRUCTIONS_H

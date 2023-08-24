//
// Created by cycastic on 8/14/23.
//

#ifndef MICROJIT_VIRTUAL_STACK_H
#define MICROJIT_VIRTUAL_STACK_H

#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include "helper.h"

namespace microjit {
    class VirtualStack {
    public:
        typedef uint8_t* StackPtr;
    private:
        const size_t stack_size;
        StackPtr stack;
        StackPtr stack_pointer;
        StackPtr base_pointer;
    public:
        // Simulate the growth downward effect of most stack
        explicit VirtualStack(const size_t& p_stack_size, const size_t& p_safe_zone = 0)
          : stack_size(p_stack_size),
            stack((StackPtr)malloc(stack_size + p_safe_zone)),
            stack_pointer((StackPtr)((size_t)stack + (stack_size + p_safe_zone))),
            base_pointer(stack_pointer){}
        ~VirtualStack(){
            free(stack);
        }
        _NO_DISCARD_ _ALWAYS_INLINE_ StackPtr* rsp() { return &stack_pointer; }
        _NO_DISCARD_ _ALWAYS_INLINE_ StackPtr* rbp() { return &base_pointer; }
        _NO_DISCARD_ _ALWAYS_INLINE_ size_t allocated() const { return (size_t)stack - (size_t)stack_pointer; }
        _NO_DISCARD_ _ALWAYS_INLINE_ size_t capacity() const { return stack_size; }
        _NO_DISCARD_ _ALWAYS_INLINE_ bool is_stack_overflown() const { return allocated() > capacity(); }
    };
}

#if defined(__cplusplus)
extern "C" {
#endif
    static void** virtual_stack_get_rsp(microjit::VirtualStack* vs){ return (void**)vs->rsp(); }
    static void** virtual_stack_get_rbp(microjit::VirtualStack* vs){ return (void**)vs->rbp(); }
    static size_t virtual_stack_get_allocated(microjit::VirtualStack* vs) { return vs->allocated(); }
    static size_t virtual_stack_get_capacity(microjit::VirtualStack* vs) { return vs->capacity(); }
    static void virtual_stack_resize_vrsp(microjit::VirtualStack* vs, size_t p_size){
        auto vrsp_ptr = virtual_stack_get_rsp(vs);
        *vrsp_ptr = (void*)((size_t)*vrsp_ptr - p_size);
    }
    static void virtual_stack_push_vrbp(microjit::VirtualStack* vs) {
        void** vrsp_ptr = virtual_stack_get_rsp(vs);
        void** vrbp_ptr = virtual_stack_get_rbp(vs);

        // push rbp
        *vrsp_ptr = (void*)((size_t)*vrsp_ptr - sizeof(decltype(vrsp_ptr)));
        *(void**)(*vrsp_ptr) = *vrbp_ptr;

        // mov rbp rsp
        *vrbp_ptr = *vrsp_ptr;
    }
    static void virtual_stack_create_stack_frame(microjit::VirtualStack* vs, size_t p_stack_frame_size) {
        void** vrsp_ptr = virtual_stack_get_rsp(vs);
        void** vrbp_ptr = virtual_stack_get_rbp(vs);

        // push rbp
        *vrsp_ptr = (void*)((size_t)*vrsp_ptr - sizeof(decltype(vrsp_ptr)));
        *(void**)(*vrsp_ptr) = *vrbp_ptr;

        // mov rbp rsp
        *vrbp_ptr = *vrsp_ptr;

        // sub rsp [p_stack_frame_size]
        *vrsp_ptr = (void*)((size_t)*vrsp_ptr - p_stack_frame_size);
    }
    static void virtual_stack_leave_stack_frame(microjit::VirtualStack* vs){
        void** vrsp_ptr = virtual_stack_get_rsp(vs);
        void** vrbp_ptr = virtual_stack_get_rbp(vs);

        // mov rsp rbp
        *vrsp_ptr = *vrbp_ptr;

        // pop rbp
        auto old_vrbp = *(void**)(*vrsp_ptr);
        *vrbp_ptr = old_vrbp;
        // Add 4/8 bytes to vrsp since it used this space to store old vrbp
        *vrsp_ptr = (void*)((size_t)*vrsp_ptr + sizeof(decltype(vrsp_ptr)));
    }
#if defined(__cplusplus)
}
#endif

#endif //MICROJIT_VIRTUAL_STACK_H

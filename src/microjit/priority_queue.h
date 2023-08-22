//
// Created by cycastic on 8/21/23.
//

#ifndef MICROJIT_EXPERIMENT_PRIORITY_QUEUE_H
#define MICROJIT_EXPERIMENT_PRIORITY_QUEUE_H

#include <vector>
#include <cstdint>
#include <stdexcept>
#include "def.h"

namespace microjit {
    template <typename T>
    struct HighCompare {
        static _ALWAYS_INLINE_ bool compare(const T& p_lhs, const T& p_rhs){
            return p_lhs > p_rhs;
        }
    };

    template <typename T>
    struct LowCompare {
        static _ALWAYS_INLINE_ bool compare(const T& p_lhs, const T& p_rhs){
            return p_lhs < p_rhs;
        }
    };

    template <typename T, class Comparator = LowCompare<uint8_t>>
    class PriorityQueue {
    public:
        struct Node {
            T data;
            uint8_t priority;
            Node(const Node& p_other) : data(p_other.data), priority(p_other.priority) {}
            Node(const T& p_data, const uint8_t& p_priority) : data(p_data), priority(p_priority) {}
            Node(T&& p_data, const uint8_t& p_priority) : data(p_data), priority(p_priority) {}
        };
    private:
        std::vector<Node> heap{};

        void heapify_up(size_t idx){
            size_t parent = (idx - 1) / 2;
            while (idx > 0 && Comparator::compare(heap[idx].priority, heap[parent].priority)){
                SWAP(heap[idx], heap[parent]);
                idx = parent;
                parent = (idx - 1) / 2;
            }
        }
        void heapify_down(size_t idx){
            while (true) {
                auto left = 2 * idx + 1;
                auto right = 2 * idx + 2;
                auto largest = idx;

                if (left < size() && Comparator::compare(heap[left].priority, heap[largest].priority))
                    largest = left;
                if (right < size() && Comparator::compare(heap[right].priority, heap[largest].priority))
                    largest = right;
                if (largest == idx) break;
                SWAP(heap[idx], heap[largest]);
                idx = largest;
            }
        }
        void base_copy(const PriorityQueue& p_other) {
            heap = p_other.heap;
        }
    protected:
        virtual void copy_from(const PriorityQueue& p_other){
            base_copy(p_other);
        }
    public:
        _NO_DISCARD_ virtual size_t size() const { return heap.size(); }
        _NO_DISCARD_ _ALWAYS_INLINE_ bool empty() const { return size() == 0; }
        _NO_DISCARD_ _ALWAYS_INLINE_ const T& operator[](const size_t p_idx) const { return heap[p_idx]; }

        virtual void push(const T& p_value, const uint8_t& p_priority){
            heap.push_back(Node(p_value, p_priority));
            heapify_up(size() - 1);
        }
        virtual T pop(){
            if (empty()) throw std::out_of_range("PriorityQueue is empty");
            auto re = heap[0].data;
            SWAP(heap[0], heap[size() - 1]);
            heap.pop_back();
            heapify_down(0);
            return re;
        }
        virtual bool try_pop(T& p_result) {
            if (PriorityQueue<T, Comparator>::size() == 0) return false;
            p_result = PriorityQueue<T, Comparator>::pop();
            return true;
        }

        PriorityQueue() = default;
        PriorityQueue(const PriorityQueue& p_other) : PriorityQueue() { base_copy(p_other); }
        _ALWAYS_INLINE_ PriorityQueue& operator=(const PriorityQueue& p_other){
            copy_from(p_other);
            return *this;
        }
    };
}

#endif //MICROJIT_EXPERIMENT_PRIORITY_QUEUE_H

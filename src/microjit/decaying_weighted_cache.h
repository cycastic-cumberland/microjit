//
// Created by cycastic on 8/22/23.
//

#ifndef MICROJIT_EXPERIMENT_DECAYING_WEIGHTED_CACHE_H
#define MICROJIT_EXPERIMENT_DECAYING_WEIGHTED_CACHE_H

#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include "def.h"
#include "helper.h"
#include "priority_queue.h"

namespace microjit {
    template <typename TKey, typename TValue,
              typename Hasher = std::hash<TKey>,
              typename Comparator = std::equal_to<TKey>,
              typename MapAllocator = std::allocator<std::pair<const TKey, Box<TValue>>>>
    class DecayingWeightedCache {
    public:
        struct Cell {
        public:
            const TKey key;
            TValue value;
            double heat{};
            std::mutex lock{};
            Cell(const TKey& p_key, const TValue& p_value)
                : key(p_key), value(p_value) {}
        };
        const Comparator comparator{};
        std::vector<Box<Cell>> heap{};
        std::unordered_map<TKey, Box<Cell>, Hasher, Comparator, MapAllocator> map{};
        size_t capacity;
        double heat_lost_per_microseconds{};
    public:
        _NO_DISCARD_ _ALWAYS_INLINE_ size_t size() const { return heap.size(); }
        _NO_DISCARD_ _ALWAYS_INLINE_ bool empty() const { return size() == 0; }
        _NO_DISCARD_ _ALWAYS_INLINE_ size_t current_capacity() const { return capacity; }
        _NO_DISCARD_ _ALWAYS_INLINE_ bool has(const TKey& p_key) const { return map.find(p_key) != map.end(); }
        _ALWAYS_INLINE_ void change_capacity(const size_t& p_cap){
            capacity = p_cap;
        }
        _NO_DISCARD_ _ALWAYS_INLINE_ double current_decay_factor() const { return heat_lost_per_microseconds / 1'000'000.0f; }
        _ALWAYS_INLINE_ void change_decay_factor(const double& p_per_second) {
            heat_lost_per_microseconds = p_per_second * 1'000'000.0f;
        }
        _NO_DISCARD_ _ALWAYS_INLINE_ Box<Cell> at(const TKey& p_key) {
            return map.at(p_key);
        }
        _NO_DISCARD_ _ALWAYS_INLINE_ Box<Cell> at(const TKey& p_key) const {
            return map.at(p_key);
        }
        _NO_DISCARD_ _ALWAYS_INLINE_ const decltype(heap)& get_heap() const {
            return heap;
        }
    private:
        _ALWAYS_INLINE_ void heapify(size_t idx, size_t n){
            auto largest = idx;
            auto l = 2 * idx + 1;
            auto r = 2 * idx + 2;
            if (l < n && LowCompare<double>::compare(heap[l]->heat, heap[largest]->heat))
                largest = l;
            if (r < n && LowCompare<double>::compare(heap[r]->heat, heap[largest]->heat))
                largest = r;
            if (largest != idx) {
                SWAP(heap[idx], heap[largest]);
                heapify(largest, n);
            }
        }
    public:
        explicit DecayingWeightedCache(const size_t& p_capacity, const double& p_heat_lost_per_seconds)
            : capacity(p_capacity) {
            change_decay_factor(p_heat_lost_per_seconds);
            heap.reserve(capacity);
        }
        Cell* push(const TKey& p_key, const TValue& p_value) noexcept {
            // Yes, this is on purpose
            if (has(p_key)) return nullptr;
            auto cell = Box<Cell>::make_box(p_key, p_value);
            map[p_key] = cell;
            heap.push_back(cell);
            return cell.ptr();
        }
        void decay(const size_t& p_microseconds_elapsed){
            static constexpr auto max_heat = double(0xFFFFFFFFFFFFFFFF);
            const auto loss = double(p_microseconds_elapsed) * heat_lost_per_microseconds;
            for (auto& elem : heap){
                elem->heat = std::clamp<double>(elem->heat - (elem->heat * loss), 0.0, max_heat);
            }
        }
        void heap_sort(){
            for (int64_t i = int64_t(size()) / 2 - 1; i >= 0; i--){
                heapify(i, size());
            }
            for (int64_t i = int64_t(size()) - 1; i > 0; i--){
                SWAP(heap[0], heap[i]);
                heapify(0, i);
            }
        }
        void manual_cleanup(){
            while (size() > capacity){
                auto removal_target = heap[size() - 1];
                map.erase(removal_target->key);
                heap.pop_back();
            }
        }
        _ALWAYS_INLINE_ void cleanup(){
            if (size() <= capacity) return;
            heap_sort();
            manual_cleanup();
        }
        void erase(const TKey& p_key){
            map.erase(p_key);
            for (auto it = heap.begin(); it != heap.end(); it++){
                if (comparator(it->ptr()->key, p_key)){
                    heap.erase(it);
                    return;
                }
            }
        }
    };
}

#endif //MICROJIT_EXPERIMENT_DECAYING_WEIGHTED_CACHE_H

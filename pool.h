#pragma once

#include <cinttypes>
#include <stdio.h>
#include <stdlib.h>

#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace van {
	namespace pool {

		
		template <class T>
		class Pool;

		using Pools = std::unordered_map<std::type_index, std::unordered_set<Pool<void>*>>;

		class IMonitor {
		public:
			virtual void created(std::type_index& tidx, Pool<void>* pool) noexcept = 0;
			virtual void deleted(std::type_index& tidx, Pool<void>* pool) noexcept = 0;
		};


		class Channel {
		private:
			std::mutex mutex_;
			Pools pools_;

			IMonitor* mon_ = nullptr;

		public:
			Channel() = default;
			Channel(const Channel&) = delete;
			Channel& operator=(const Channel&) = delete;

			static Channel& inst()
			{
				static Channel inst;
				return inst;
			}

			void set(IMonitor* mon) noexcept
			{
				std::lock_guard<std::mutex> lock(mutex_);
				mon_ = mon;
				pass_all();
			}

			template <class T>
			void created(Pool<T>* p) noexcept
			{
				std::type_index tidx = typeid(T);
				Pool<void>* pool = reinterpret_cast<Pool<void>*>(p);

				std::lock_guard<std::mutex> lock(mutex_);

				if (mon_) {
					mon_->created(tidx, pool);
					pass_all();
				} else {
					pools_[tidx].insert(pool);
				}
			}

			template <class T>
			void deleted(Pool<T>* p) noexcept
			{
				std::type_index tidx = typeid(T);
				Pool<void>* pool = reinterpret_cast<Pool<void>*>(p);

				std::lock_guard<std::mutex> lock(mutex_);

				if (mon_) {
					mon_->deleted(tidx, pool);
					pass_all();
				} else {
					pools_[tidx].erase(pool);
				}
			}

		private:
			void pass_all() noexcept
			{
				if (!mon_) return;
				if (pools_.empty()) return;

				for (auto it : pools_) {
					auto tidx = it.first;
					auto& poolset = it.second;

					for (auto pool : poolset) {
						mon_->created(tidx, pool);
					}
				}
			}

		};

		template <class T>
		class Pool {
		private:

			struct Obj {
				T inst_;
				Obj* next_;
			};
			Obj* curr_ = nullptr;
			Obj* last_ = nullptr;
			Obj* free_ = nullptr;

			struct Block {
				Block* next_;
			};
			Block* blocks_ = nullptr;

			int cnt_ = 128;

			uint64_t total_cnt_ = 0;
			uint64_t use_cnt_ = 0;

		public:
			using value_type = T;

		public:

			Pool(int cnt = 0) noexcept
			{
				if (cnt > 0) {
					cnt_ = cnt;
					new_block();
				}

				Channel::inst().created(this);
			}

			~Pool() noexcept
			{
				Channel::inst().deleted(this);

				Block* block = blocks_;
				while (block) {
					Block* next = block->next_;
					free(block);
					block = next;
				}
			}

			Pool(const Pool<T>&) = delete;
			Pool& operator=(const Pool<T>&) = delete;

			T* get() noexcept
			{
				++use_cnt_;

				if (free_) {
					Obj* obj = free_;
					free_ = free_->next_;
					return &(obj->inst_);
				}
				if (curr_ >= last_) {
					new_block();
				}
				return &((curr_++)->inst_);
				
			}

			void ret(T* t) noexcept
			{
				--use_cnt_;
				
				Obj* obj = reinterpret_cast<Obj*>(t);
				obj->next_ = free_;
				free_ = obj;
			}


			uint64_t total_cnt() noexcept
			{
				return total_cnt_;
			}

			uint64_t use_cnt() noexcept
			{
				return use_cnt_;
			}

		private:
			void new_block() noexcept
			{
				Block* block = reinterpret_cast<Block*>(malloc(sizeof(Block) + (sizeof(Obj) * cnt_)));
				block->next_ = blocks_;
				blocks_ = block;

				curr_ = reinterpret_cast<Obj*>(block + 1);
				last_  = curr_ + cnt_;

				total_cnt_ += cnt_;
			}

		};

		template <int size>
		class Mem {
		private:
			static_assert(size > 0, "too small size");

		public:
			static constexpr int len_ = size;
			char buf_[size];
		};



		/*******************************************
		 * call construct, destruct
		 *******************************************/
		template <class T, class... Args>
		void construct(T* t, Args&&... args) noexcept
		{
			new (t) T (std::forward<Args>(args)...);
		}

		template <class T>
		void destruct(T* t) noexcept
		{
			t->~T();
		}


		/*******************************************
		 * tls pool
		 *******************************************/
		template <class T>
		Pool<T>& get_tls_pool(int cnt = 0) noexcept
		{
			thread_local Pool<T> pool(cnt);
			return pool;
		}

		template <class T> 
		void warm_up_tls_pool(int cnt) noexcept
		{
			get_tls_pool<T>(cnt);
		}

		template <class T>
		T* get_tls() noexcept
		{
			return get_tls_pool<T>().get();
		}

		template <class T>
		void ret_tls(T* t) noexcept
		{
			get_tls_pool<T>().ret(t);
		}

		template <int size>
		void warm_up_tls_pool(int cnt) noexcept
		{
			using T = Mem<size>;
			get_tls_pool<T>(cnt);
		}

		template <int size>
		Mem<size>* get_tls() noexcept
		{
			using T = Mem<size>;
			return get_tls_pool<T>().get();
		}


		/*******************************************
		 * singleton pool
		 *******************************************/
		template <class T>
		Pool<T>& get_singleton_pool(int cnt = 0) noexcept
		{
			static Pool<T> pool(cnt);
			return pool;
		}

		template <class T>
		std::mutex& get_singleton_mutex() noexcept
		{
			static std::mutex mutex;
			return mutex;
		}

		template <class T>
		void warm_up_singleton(int cnt) noexcept
		{
			std::lock_guard<std::mutex> lock(get_singleton_mutex<T>());
			get_singleton_pool<T>(cnt);
		}

		template <class T>
		T* get_singleton() noexcept
		{
			std::lock_guard<std::mutex> lock(get_singleton_mutex<T>());
			return get_singleton_pool<T>().get();
		}

		template <class T>
		void ret_singleton(T* t) noexcept
		{
			std::lock_guard<std::mutex> lock(get_singleton_mutex<T>());
			get_singleton_pool<T>().ret(t);
		}

		template <int size>
		void warm_up_singleton(int cnt) noexcept
		{
			using T = Mem<size>;
			std::lock_guard<std::mutex> lock(get_singleton_mutex<T>());
			get_singleton_pool<T>(cnt);
		}

		template <int size>
		Mem<size>* get_singleton() noexcept
		{
			using T = Mem<size>;
			std::lock_guard<std::mutex> lock(get_singleton_mutex<T>());
			return get_singleton_pool<T>().get();
		}


		/*******************************************
		 * monitor
		 *******************************************/
		class Count {
			public:
				uint64_t total_ = 0;
				uint64_t use_ = 0;
				uint64_t pool_ = 0;
		};

		using Stat = std::unordered_map<std::type_index, Count>;


		class Monitor : public IMonitor {
		private:
			std::mutex mutex_;
			Pools pools_;

		public:
			Monitor() noexcept
			{
				Channel::inst().set(this);
			}

			~Monitor() noexcept
			{
				Channel::inst().set(nullptr);
			}

			static Monitor& inst()
			{
				static Monitor inst;
				return inst;
			}

			virtual void created(std::type_index& tidx, Pool<void>* pool) noexcept override
			{
				std::lock_guard<std::mutex> lock(mutex_);
				pools_[tidx].insert(pool);
			}

			virtual void deleted(std::type_index& tidx, Pool<void>* pool) noexcept override
			{
				std::lock_guard<std::mutex> lock(mutex_);
				auto& poolset = pools_[tidx];
				poolset.erase(pool);
				if (poolset.empty()) {
					pools_.erase(tidx);
				}
			}

			Stat stat() noexcept
			{
				std::lock_guard<std::mutex> lock(mutex_);

				Stat stat;
				for (auto it : pools_) {
					auto& tidx = it.first;
					auto& poolset = it.second;

					Count cnt;
					for (auto* pool : poolset) {
						cnt.total_ += pool->total_cnt();
						cnt.use_ += pool->use_cnt();
					}
					cnt.pool_ = poolset.size();
					stat[tidx] = cnt;
				}
				return stat;
			}

		};

		static void print_stat() noexcept
		{
			Stat s = Monitor::inst().stat();

			printf(
				"%4s %-30s %10s %10s %10s\n",
				"NO.", "CLASS", "POOL", "TOTAL", "USE"
			);

			int no = 0;
			for (auto it : s) {
				auto& tidx = it.first;
				auto& cnt = it.second;
				printf(
					"%3d. %-30s %10" PRIu64" %10" PRIu64" %10" PRIu64"\n",
					++no, tidx.name(), cnt.pool_, cnt.total_, cnt.use_
				);
			}
		}

	}
}


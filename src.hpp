#ifndef SRC_HPP
#define SRC_HPP

#include <cstddef>

/**
 * 枚举类，用于枚举可能的置换策略
 */
enum class ReplacementPolicy { kDEFAULT = 0, kFIFO, kLRU, kMRU, kLRU_K };

/**
 * @brief 该类用于维护每一个页对应的信息以及其访问历史，用于在尝试置换时查询需要的信息。
 */
class PageNode {
public:
  std::size_t page_id_;
  std::size_t arrival_time_;
  std::size_t* access_history_;
  std::size_t access_count_;
  std::size_t k_;

  PageNode(std::size_t id, std::size_t time, std::size_t k)
      : page_id_(id), arrival_time_(time), access_count_(0), k_(k) {
    access_history_ = new std::size_t[k];
    for (std::size_t i = 0; i < k; ++i) access_history_[i] = 0;
    add_access(time);
  }

  ~PageNode() {
    delete[] access_history_;
  }

  void add_access(std::size_t time) {
    if (access_count_ < k_) {
      access_history_[access_count_++] = time;
    } else {
      for (std::size_t i = 0; i < k_ - 1; ++i) {
        access_history_[i] = access_history_[i + 1];
      }
      access_history_[k_ - 1] = time;
    }
  }

  std::size_t get_latest_time() const {
    return access_history_[access_count_ - 1];
  }
};

class ReplacementManager {
public:
  constexpr static std::size_t npos = -1;

  ReplacementManager() = delete;

  /**
   * @brief 初始化整个类
   * @param max_size 缓存池可以容纳的页数量的上限
   * @param k LRU-K所基于的常数k，在类销毁前不会变更
   * @param default_policy 在置换时，如果没有显式指示，则默认使用default_policy作为策略
   * @note 我们将保证default_policy的值不是ReplacementPolicy::kDEFAULT。
   */
  ReplacementManager(std::size_t max_size, std::size_t k, ReplacementPolicy default_policy)
      : max_size_(max_size), k_(k), default_policy_(default_policy), size_(0), current_time_(0) {
    nodes_ = (max_size > 0) ? new PageNode*[max_size] : nullptr;
    for (std::size_t i = 0; i < max_size; ++i) nodes_[i] = nullptr;
  }

  /**
   * @brief 析构函数
   * @note 我们将对代码进行Valgrind Memcheck，请保证你的代码不发生内存泄漏
   */
  ~ReplacementManager() {
    for (std::size_t i = 0; i < size_; ++i) {
      delete nodes_[i];
    }
    delete[] nodes_;
  }

  /**
   * @brief 重设当前默认的缓存置换政策
   * @param default_policy 新的默认政策，保证default_policy不是ReplacementPolicy::kDEFAULT
   */
  void SwitchDefaultPolicy(ReplacementPolicy default_policy) {
    default_policy_ = default_policy;
  }

  /**
   * @brief 访问某个页面。
   * @param page_id 访问页的编号
   * @param evict_id 需要被置换的页编号，如果不需要置换请将其设置为npos
   * @param policy 如果需要置换，那么置换所基于的策略
   * (a) 若访问的页已经在缓存池中，那么直接记录其访问信息。
   * (b) 若访问的页不在缓存池中，那么：
   *    1. 若缓存池已满，就从中依照policy置换一个页（彻底删除其对应节点），并将新访问的页加入缓存池，记录其访问
   *    2. 若缓存池未满，则直接将其加入缓存池并记录其访问
   * @note 我们不保证page_id在调用间连续，也不保证page_id的范围，只保证page_id在std::size_t内
   */
  void Visit(std::size_t page_id, std::size_t &evict_id, ReplacementPolicy policy = ReplacementPolicy::kDEFAULT) {
    current_time_++;
    evict_id = npos;

    for (std::size_t i = 0; i < size_; ++i) {
      if (nodes_[i]->page_id_ == page_id) {
        nodes_[i]->add_access(current_time_);
        return;
      }
    }

    if (size_ < max_size_) {
      nodes_[size_++] = new PageNode(page_id, current_time_, k_);
    } else if (max_size_ > 0) {
      ReplacementPolicy actual_policy = (policy == ReplacementPolicy::kDEFAULT) ? default_policy_ : policy;
      std::size_t victim_idx = find_victim(actual_policy);
      evict_id = nodes_[victim_idx]->page_id_;
      delete nodes_[victim_idx];
      nodes_[victim_idx] = new PageNode(page_id, current_time_, k_);
    }
  }

  /**
   * @brief 强制地删除特定的页（无论缓存池是否已满）
   * @param page_id 被删除页的编号
   * @return 如果成功删除，则返回true; 如果该页不存在于缓存池中，则返回false
   * 如果page_id存在于缓存池中，则删除它；否则，直接返回false
   */
  bool RemovePage(std::size_t page_id) {
    for (std::size_t i = 0; i < size_; ++i) {
      if (nodes_[i]->page_id_ == page_id) {
        delete nodes_[i];
        if (i < size_ - 1) {
          nodes_[i] = nodes_[size_ - 1];
        }
        size_--;
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 查询特定策略下首先被置换的页
   * @param policy 置换策略
   * @return 当前策略下会被置换的页的编号。若缓存池没满，则返回npos
   * 不对缓存池做任何修改，只查询在需要置换的情况下，基于给定的政策，应该置换哪个页。
   * @note 如果缓存池没有满，请直接返回npos
   */
  [[nodiscard]] std::size_t TryEvict(ReplacementPolicy policy = ReplacementPolicy::kDEFAULT) const {
    if (size_ < max_size_ || max_size_ == 0) return npos;
    ReplacementPolicy actual_policy = (policy == ReplacementPolicy::kDEFAULT) ? default_policy_ : policy;
    std::size_t victim_idx = find_victim(actual_policy);
    return nodes_[victim_idx]->page_id_;
  }

  /**
   * @brief 返回当前缓存管理器是否为空。
   */
  [[nodiscard]] bool Empty() const {
    return size_ == 0;
  }

  /**
   * @brief 返回当前缓存管理器是否已满（即是否页数量已经达到上限）
   */
  [[nodiscard]] bool Full() const {
    return size_ == max_size_;
  }

  /**
   * @brief 返回当前缓存管理器中页的数量
   */
  [[nodiscard]] std::size_t Size() const {
    return size_;
  }

private:
  std::size_t find_victim(ReplacementPolicy policy) const {
    std::size_t victim = 0;
    if (policy == ReplacementPolicy::kFIFO) {
      for (std::size_t i = 1; i < size_; ++i) {
        if (nodes_[i]->arrival_time_ < nodes_[victim]->arrival_time_) {
          victim = i;
        }
      }
    } else if (policy == ReplacementPolicy::kLRU) {
      for (std::size_t i = 1; i < size_; ++i) {
        if (nodes_[i]->get_latest_time() < nodes_[victim]->get_latest_time()) {
          victim = i;
        }
      }
    } else if (policy == ReplacementPolicy::kMRU) {
      for (std::size_t i = 1; i < size_; ++i) {
        if (nodes_[i]->get_latest_time() > nodes_[victim]->get_latest_time()) {
          victim = i;
        }
      }
    } else if (policy == ReplacementPolicy::kLRU_K) {
      bool victim_less_than_k = (nodes_[0]->access_count_ < k_);
      for (std::size_t i = 1; i < size_; ++i) {
        bool current_less_than_k = (nodes_[i]->access_count_ < k_);
        if (current_less_than_k && !victim_less_than_k) {
          victim = i;
          victim_less_than_k = true;
        } else if (current_less_than_k == victim_less_than_k) {
          if (nodes_[i]->access_history_[0] < nodes_[victim]->access_history_[0]) {
            victim = i;
          }
        }
      }
    }
    return victim;
  }

  PageNode** nodes_;
  std::size_t size_;
  std::size_t max_size_;
  std::size_t k_;
  std::size_t current_time_;
  ReplacementPolicy default_policy_;
};
#endif

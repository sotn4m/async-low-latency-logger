#include "logger/async_logger.hpp"

// TODO(milestone-5): #include <fstream> and <thread> once Impl actually
// holds an std::ofstream and std::jthread (removed for now since an
// unused include is dead weight — add them back when you fill in Impl).

namespace alll {

struct AsyncLogger::Impl {
  // TODO(milestone-5): std::ofstream out_file;
  // TODO(milestone-5): std::jthread consumer; — construct this LAST in
  // AsyncLogger's constructor init list / body, after out_file is open,
  // since the consumer thread starts running immediately and will touch
  // out_file right away.
};

AsyncLogger::AsyncLogger(std::string_view path) : impl_(std::make_unique<Impl>()) {
  // TODO(milestone-5): open impl_->out_file at `path` (append mode), then
  // start impl_->consumer running this->consume(...).
  (void)path;
}

AsyncLogger::~AsyncLogger() = default;

std::size_t AsyncLogger::dropped_count() const noexcept {
  return dropped_count_.load(std::memory_order_relaxed);
}

void AsyncLogger::push_record(LogRecord&& record) {
  // TODO(milestone-4/5): if (!ring_.try_push(std::move(record))) increment
  // dropped_count_ with memory_order_relaxed (see the reasoning in the
  // header comment) and return. Never retry, never block.
  (void)record;
}

void AsyncLogger::consume(std::stop_token stop_token) {
  // TODO(milestone-5): see the numbered design notes in the header.
  (void)stop_token;
}

} // namespace alll

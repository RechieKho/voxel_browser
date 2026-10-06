#include "vb/auth/signin.hpp"

#include <utility>

namespace vb::auth {

SignInTask::SignInTask(Work work) {
	worker_ = std::thread([this, work = std::move(work)] {
		SignInResult r = work(cancelled_);
		r.done = true;
		std::lock_guard lock(mu_);
		result_ = std::move(r);
	});
}

SignInTask::~SignInTask() {
	cancel();
	if (worker_.joinable()) {
		worker_.join();
	}
}

void SignInTask::cancel() { cancelled_.store(true); }

SignInResult SignInTask::poll() const {
	std::lock_guard lock(mu_);
	return result_;
}

} // namespace vb::auth

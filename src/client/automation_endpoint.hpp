// Client half of the development-only automation channel
// (docs/e2e-automation.md §5.1, contract in docs/automation-protocol.md).
// Only included under VB_WITH_AUTOMATION.
//
// E2: read-only queries. E3: input / UI / chat commands and the multi-frame
// actions (walk_to, break_block, place_block) built on top of them. Actions feed
// a SyntheticInput that the run loop hands to ClientApp::frame(), so they
// exercise exactly the code path a player's keyboard and mouse do.
#pragma once

#include <memory>
#include <vector>

#include "client_app.hpp"
#include "vb/automation/host.hpp"
#include "vb/render/input.hpp"

namespace vb::client {

class ClientAutomationEndpoint final : public vb::automation::Endpoint {
public:
	ClientAutomationEndpoint(ClientApp &app, vb::automation::Host &host);
	~ClientAutomationEndpoint() override;

	// Input the next ClientApp::frame() should be given.
	vb::render::InputSource &input() { return input_; }

	// Run-loop hooks, once per frame around ClientApp::frame():
	//   begin_frame(); app.frame(input().poll(), dt); end_frame();
	// begin_frame lets running actions queue this frame's input; end_frame
	// checks their completion and answers the deferred replies.
	void begin_frame();
	void end_frame();

	std::string role() const override { return "client"; }
	nlohmann::json state() override;
	std::optional<std::string> block_name_at(int x, int y, int z) override;
	std::optional<vb::automation::Reply> command(const vb::automation::Request &req) override;

	struct Task;
	struct Ctx;

private:
	ClientApp &app_;
	vb::automation::Host &host_;
	vb::render::SyntheticInput input_;
	std::vector<std::unique_ptr<Task>> tasks_;
	long long frame_ = 0;
};

} // namespace vb::client

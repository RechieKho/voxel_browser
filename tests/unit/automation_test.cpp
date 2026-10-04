// Unit tests for the development-only automation module (src/automation/).
#include <doctest/doctest.h>

#include <chrono>
#include <map>
#include <sstream>
#include <thread>

#include "vb/automation/host.hpp"
#include "vb/automation/predicate.hpp"
#include "vb/automation/protocol.hpp"
#include "vb/core/build_info.hpp"

using namespace vb::automation;
using nlohmann::json;

namespace {

bool eval(const json &pred, const json &state, std::string *err = nullptr,
		json *observed = nullptr) {
	PredicateContext ctx{ state, [](int x, int y, int z) -> std::optional<std::string> {
							 return (x == 4 && y == 70 && z == 4) ? std::optional<std::string>("base:stone")
																  : std::nullopt;
						 } };
	EvalResult r = evaluate_predicate(pred, ctx);
	if (err)
		*err = r.error;
	if (observed)
		*observed = r.observed;
	return r.matched;
}

struct TestEndpoint : Endpoint {
	json st = json{ { "joined", true }, { "chat", json::array({ "<A> hello" }) } };
	std::string role() const override { return "client"; }
	json state() override { return st; }
	std::optional<Reply> command(const Request &r) override {
		if (r.cmd == "echo")
			return Reply::success(r.args);
		return std::nullopt;
	}
};

// Runs lines through a Host until stdin EOF, returns the response frames.
std::vector<json> run(const std::string &input, TestEndpoint &ep, bool manual = false) {
	std::istringstream in(input);
	std::ostringstream out;
	{
		Host host(in, out);
		host.set_manual_clock(manual);
		for (int i = 0; i < 2000 && host.pump(ep); ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	std::vector<json> frames;
	std::istringstream lines(out.str());
	for (std::string l; std::getline(lines, l);) {
		frames.push_back(json::parse(l));
	}
	return frames;
}

} // namespace

TEST_CASE("automation: build identifies itself") {
	CHECK(vb::core::describe_build().find("+automation") != std::string::npos);
}

TEST_CASE("automation: request parsing") {
	std::string err;
	auto r = parse_request(R"({"id":3,"cmd":"state","args":{"a":1}})", err);
	REQUIRE(r);
	CHECK(r->cmd == "state");
	CHECK(r->id == 3);
	CHECK(r->args["a"] == 1);

	CHECK_FALSE(parse_request("not json", err));
	CHECK_FALSE(parse_request(R"([1])", err));
	CHECK_FALSE(parse_request(R"({"id":1})", err));
	CHECK_FALSE(parse_request(R"({"id":[1],"cmd":"x"})", err));
	CHECK_FALSE(parse_request(R"({"id":1,"cmd":"x","args":3})", err));

	auto h = parse_request(R"({"id":1,"cmd":"hello","proto":1})", err);
	REQUIRE(h);
	CHECK(h->args["proto"] == 1);
}

TEST_CASE("automation: response framing") {
	const json ok = make_response(7, Reply::success(json{ { "x", 1 } }));
	CHECK(ok["id"] == 7);
	CHECK(ok["ok"] == true);
	CHECK(ok["result"]["x"] == 1);
	const json bad = make_response("a", Reply::error("timeout", "m", json{ { "last", 5 } }));
	CHECK(bad["ok"] == false);
	CHECK(bad["error"]["code"] == "timeout");
	CHECK(bad["error"]["last"] == 5);
	CHECK(make_event("log", json{ { "msg", "x" } })["event"] == "log");
}

TEST_CASE("automation: leaf predicates") {
	const json st = json::parse(R"({
		"joined": true, "app_state": "playing", "health": 15, "chunks_loaded": 40,
		"feet": [1.0, 70.0, 2.0], "player_count": 2,
		"chat": ["<A> hello", "<B> hi there"],
		"entities": [{"name":"B","pos":[4,70,4]}],
		"players": [{"name":"A","pos":[0,70,0]}],
		"ui": {"name":"base:inventory","widgets":[{"id":"craft","text":"Craft"}]},
		"inventory": [{"item":"base:planks","count":3},{"item":"base:planks","count":2}]
	})");
	CHECK(eval({ { "joined", true } }, st));
	CHECK(eval({ { "app_state", { { "is", "playing" } } } }, st));
	CHECK(eval({ { "app_state", "playing" } }, st));
	CHECK_FALSE(eval({ { "app_state", "menu" } }, st));
	CHECK(eval({ { "chat_contains", "hello" } }, st));
	CHECK(eval({ { "chat_contains", { { "regex", "^<B> h.*e$" } } } }, st));
	CHECK_FALSE(eval({ { "chat_contains", "nope" } }, st));
	CHECK(eval({ { "entity_visible", { { "name", "B" } } } }, st));
	CHECK(eval({ { "entity_near", { { "name", "B" }, { "pos", { 4, 70, 5 } }, { "radius", 2 } } } }, st));
	CHECK_FALSE(eval({ { "entity_near", { { "name", "B" }, { "pos", { 40, 70, 5 } }, { "radius", 2 } } } }, st));
	CHECK(eval({ { "player_near", { { "name", "A" }, { "pos", { 0, 70, 0 } }, { "radius", 1 } } } }, st));
	CHECK(eval({ { "block_is", { { "pos", { 4, 70, 4 } }, { "block", "base:stone" } } } }, st));
	CHECK_FALSE(eval({ { "block_is", { { "pos", { 4, 70, 4 } }, { "block", "base:air" } } } }, st));
	CHECK_FALSE(eval({ { "block_is", { { "pos", { 9, 9, 9 } }, { "block", "base:air" } } } }, st));
	CHECK(eval({ { "ui_open", "base:inventory" } }, st));
	CHECK(eval({ { "widget", { { "id", "craft" }, { "text", "Craft" } } } }, st));
	CHECK_FALSE(eval({ { "widget", { { "id", "craft" }, { "text", "x" } } } }, st));
	CHECK(eval({ { "pos_near", { { "pos", { 1, 70, 3 } }, { "radius", 2.5 } } } }, st));
	CHECK(eval({ { "inventory_has", { { "item", "base:planks" }, { "count", 5 } } } }, st));
	CHECK_FALSE(eval({ { "inventory_has", { { "item", "base:planks" }, { "count", 6 } } } }, st));
	CHECK(eval({ { "health", { { "op", ">=" }, { "value", 15 } } } }, st));
	CHECK_FALSE(eval({ { "health", { { "op", "<" }, { "value", 15 } } } }, st));
	CHECK(eval({ { "chunks_loaded", 40 } }, st));
	CHECK_FALSE(eval({ { "chunks_loaded", { { "min", 41 } } } }, st));
	CHECK(eval({ { "player_count", 2 } }, st));
}

TEST_CASE("automation: predicates tolerate missing state") {
	const json empty = json::object();
	std::string err;
	CHECK_FALSE(eval({ { "joined", true } }, empty, &err));
	CHECK(err.empty());
	CHECK_FALSE(eval({ { "chat_contains", "x" } }, empty, &err));
	CHECK_FALSE(eval({ { "health", 10 } }, empty, &err));
	CHECK(err.empty());
}

TEST_CASE("automation: combinators and observed values") {
	const json st = json::parse(R"({"joined":true,"chat":["<A> hello"]})");
	CHECK(eval(json::parse(R"({"all":[{"joined":true},{"chat_contains":"hello"}]})"), st));
	CHECK_FALSE(eval(json::parse(R"({"all":[{"joined":true},{"chat_contains":"bye"}]})"), st));
	CHECK(eval(json::parse(R"({"any":[{"chat_contains":"bye"},{"joined":true}]})"), st));
	CHECK(eval(json::parse(R"({"not":{"chat_contains":"bye"}})"), st));
	json observed;
	eval({ { "chat_contains", "bye" } }, st, nullptr, &observed);
	CHECK(observed["chat"][0] == "<A> hello"); // timeout replies say *why*
}

TEST_CASE("automation: malformed predicates are errors, not silent false") {
	std::string err;
	const json st = json::object();
	eval({ { "no_such", 1 } }, st, &err);
	CHECK_FALSE(err.empty());
	eval(json::parse(R"({"a":1,"b":2})"), st, &err);
	CHECK_FALSE(err.empty());
	eval(json::parse(R"({"all":[]})"), st, &err);
	CHECK_FALSE(err.empty());
	eval(json::parse(R"({"health":{"op":"~","value":1}})"), json{ { "health", 3 } }, &err);
	CHECK_FALSE(err.empty());
	eval(json::parse(R"({"chat_contains":{"regex":"("}})"), st, &err);
	CHECK_FALSE(err.empty());
	json deep = json{ { "joined", true } };
	for (int i = 0; i < 40; ++i)
		deep = json{ { "not", deep } };
	eval(deep, st, &err);
	CHECK_FALSE(err.empty());
}

TEST_CASE("automation: host answers commands in order") {
	TestEndpoint ep;
	const auto f = run(
			"{\"id\":1,\"cmd\":\"hello\",\"proto\":1}\n"
			"{\"id\":2,\"cmd\":\"state\"}\n"
			"{\"id\":3,\"cmd\":\"echo\",\"args\":{\"k\":\"v\"}}\n"
			"{\"id\":4,\"cmd\":\"bogus\"}\n"
			"garbage\n"
			"{\"id\":5,\"cmd\":\"hello\",\"proto\":99}\n"
			"{\"id\":6,\"cmd\":\"quit\"}\n",
			ep);
	REQUIRE(f.size() == 7);
	CHECK(f[0]["result"]["role"] == "client");
	CHECK(f[0]["result"]["build"].get<std::string>().find("+automation") != std::string::npos);
	CHECK(f[1]["result"]["joined"] == true);
	CHECK(f[2]["result"]["k"] == "v");
	CHECK(f[3]["error"]["code"] == "unknown_command");
	CHECK(f[4]["error"]["code"] == "bad_request");
	CHECK(f[5]["error"]["code"] == "proto_mismatch");
	CHECK(f[6]["ok"] == true);
}

TEST_CASE("automation: wait_for resolves, times out, and rejects bad predicates") {
	TestEndpoint ep;
	std::istringstream in(
			"{\"id\":1,\"cmd\":\"wait_for\",\"args\":{\"pred\":{\"joined\":true}}}\n"
			"{\"id\":2,\"cmd\":\"wait_for\",\"args\":{\"pred\":{\"chat_contains\":\"later\"},\"timeout_ms\":50}}\n"
			"{\"id\":3,\"cmd\":\"wait_for\",\"args\":{\"pred\":{\"chat_contains\":\"soon\"},\"timeout_ms\":5000}}\n"
			"{\"id\":4,\"cmd\":\"wait_for\",\"args\":{\"pred\":{\"zzz\":1}}}\n");
	std::ostringstream out;
	Host host(in, out);
	const auto t0 = Host::Clock::now();
	// Pump until all four requests were taken off the queue.
	for (int i = 0; i < 2000 && out.str().find("\"id\":4") == std::string::npos; ++i) {
		host.pump(ep, t0);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	ep.st["chat"].push_back("<B> soon");
	host.pump(ep, t0 + std::chrono::milliseconds(10));
	host.pump(ep, t0 + std::chrono::milliseconds(100)); // id 2's 50ms deadline passed

	std::map<int, json> by_id;
	std::istringstream lines(out.str());
	for (std::string l; std::getline(lines, l);) {
		json j = json::parse(l);
		by_id[j["id"].get<int>()] = j;
	}
	CHECK(by_id.at(1)["ok"] == true);
	CHECK(by_id.at(2)["error"]["code"] == "timeout");
	CHECK(by_id.at(2)["error"]["last"]["chat"][0] == "<A> hello");
	CHECK(by_id.at(3)["ok"] == true);
	CHECK(by_id.at(4)["error"]["code"] == "bad_request");
}

TEST_CASE("automation: step needs manual clock and completes after frames run") {
	TestEndpoint ep;
	{
		const auto f = run("{\"id\":1,\"cmd\":\"step\",\"args\":{\"frames\":2}}\n", ep, false);
		REQUIRE(f.size() == 1);
		CHECK(f[0]["error"]["code"] == "unsupported");
	}
	std::istringstream in("{\"id\":9,\"cmd\":\"step\",\"args\":{\"frames\":3}}\n");
	std::ostringstream out;
	Host host(in, out);
	host.set_manual_clock(true);
	CHECK_FALSE(host.frame_allowed());
	for (int i = 0; i < 2000 && !host.frame_allowed(); ++i) {
		host.pump(ep);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE(host.frame_allowed());
	host.frame_done();
	host.frame_done();
	CHECK(out.str().empty());
	host.frame_done();
	CHECK(json::parse(out.str())["result"]["frames"] == 3);
	CHECK_FALSE(host.frame_allowed());
}

TEST_CASE("automation: stdin EOF asks the process to exit") {
	TestEndpoint ep;
	std::istringstream in("");
	std::ostringstream out;
	Host host(in, out);
	bool alive = true;
	for (int i = 0; i < 2000 && alive; ++i) {
		alive = host.pump(ep);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK_FALSE(alive);
}

TEST_CASE("automation: a deferred reply is sent by Host::respond, not at dispatch") {
	struct DeferEndpoint : TestEndpoint {
		std::optional<Reply> command(const Request &r) override {
			if (r.cmd == "later") {
				return Reply::defer();
			}
			return std::nullopt;
		}
	} ep;
	std::istringstream in("{\"id\":7,\"cmd\":\"later\"}\n");
	std::ostringstream out;
	Host host(in, out);
	for (int i = 0; i < 2000 && host.pump(ep); ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(out.str().empty()); // dispatched, but nothing answered yet
	host.respond(json(7), Reply::success(json{ { "done", true } }));
	const json frame = json::parse(out.str());
	CHECK(frame["id"] == 7);
	CHECK(frame["ok"] == true);
	CHECK(frame["result"]["done"] == true);
}

TEST_CASE("automation: chunk_loaded asks the process, and a missing hook is just not-loaded") {
	const json state = json::object();
	const PredicateContext ctx{ state, nullptr, [](int x, int, int) { return x < 10; } };
	const json at = json{ { "chunk_loaded", json{ { "pos", json::array({ 0, 0, 0 }) } } } };
	const json far = json{ { "chunk_loaded", json{ { "pos", json::array({ 50, 0, 0 }) } } } };
	CHECK(evaluate_predicate(at, ctx).matched);
	CHECK_FALSE(evaluate_predicate(far, ctx).matched);

	const PredicateContext none{ state };
	const EvalResult r = evaluate_predicate(at, none);
	CHECK_FALSE(r.matched);
	CHECK(r.error.empty());

	CHECK_FALSE(evaluate_predicate(json{ { "chunk_loaded", json::object() } }, ctx).error.empty());
}

TEST_CASE("automation: on_ground reads the snapshot and is false when absent") {
	CHECK(eval(json{ { "on_ground", true } }, json{ { "on_ground", true } }));
	CHECK_FALSE(eval(json{ { "on_ground", true } }, json{ { "on_ground", false } }));
	CHECK_FALSE(eval(json{ { "on_ground", true } }, json::object()));
}

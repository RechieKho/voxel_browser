// Declarative wait/assert predicates (docs/e2e-automation.md §5.2), evaluated
// in-process against a JSON state snapshot every frame/tick so tests never poll
// over the pipe. Composable with all/any/not.
//
// Snapshot fields read (missing fields read as "not true", never an error):
//   joined(bool) app_state(str) feet([x,y,z]) health(num) chunks_loaded(int)
//   chat([str]) entities([{name,pos}]) ui({name,widgets[{id,text}]})
//   inventory([{item,count}]) players([{name,pos}]) player_count(int)
#pragma once

#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace vb::automation {

struct PredicateContext {
	const nlohmann::json &state;
	// Block registry *name* (e.g. "base:air") at a world voxel, if loaded.
	std::function<std::optional<std::string>(int, int, int)> block_name_at;
};

struct EvalResult {
	bool matched = false;
	// What the predicate actually saw; reported as `last` on timeout so a
	// failure says why (the toHaveText analogue).
	nlohmann::json observed;
	// Non-empty when the predicate itself is malformed (unknown name, bad args).
	std::string error;
};

EvalResult evaluate_predicate(const nlohmann::json &pred, const PredicateContext &ctx);

} // namespace vb::automation

#include "vb/cli/completions.hpp"

#include <sstream>

namespace vb::cli {

namespace {

std::string join(const std::vector<std::string> &v, const char *sep = " ") {
	std::string out;
	for (const std::string &s : v) {
		out += (out.empty() ? "" : sep) + s;
	}
	return out;
}

std::string names(const CompletionSpec &spec) {
	std::vector<std::string> n;
	for (const auto &[name, summary] : spec.commands) {
		n.push_back(name);
	}
	return join(n);
}

std::string escape_single(const std::string &s) {
	std::string out;
	for (const char c : s) {
		out += c == '\'' ? std::string("''") : std::string(1, c);
	}
	return out;
}

// Commands whose first argument is an installed version / link name.
const char *kVersionCommands = "use uninstall unlink which";
// server subcommands whose first argument is an instance name.
const char *kInstanceSubcommands = "start stop restart status logs rm config";

std::string bash(const CompletionSpec &spec) {
	std::ostringstream os;
	os << "# bash completion for vb -- source it, or save as ~/.local/share/bash-completion/completions/vb\n"
	   << "_vb() {\n"
	   << "  local cur=\"${COMP_WORDS[COMP_CWORD]}\" prev=\"\"\n"
	   << "  [ \"$COMP_CWORD\" -gt 0 ] && prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
	   << "  local cmd=\"${COMP_WORDS[1]}\" sub=\"${COMP_WORDS[2]}\"\n"
	   << "  local words=\"\"\n"
	   << "  if [ \"$prev\" = \"--version\" ]; then\n"
	   << "    words=\"$(vb __complete versions 2>/dev/null)\"\n"
	   << "  elif [ \"$COMP_CWORD\" -eq 1 ]; then\n"
	   << "    words=\"" << names(spec) << " --help --version\"\n"
	   << "  elif [ \"$cmd\" = \"server\" ]; then\n"
	   << "    if [ \"$COMP_CWORD\" -eq 2 ]; then\n"
	   << "      words=\"" << join(spec.server_subcommands) << "\"\n"
	   << "    elif [ \"$COMP_CWORD\" -eq 3 ]; then\n"
	   << "      case \" " << kInstanceSubcommands << " \" in *\" $sub \"*) words=\"$(vb __complete instances 2>/dev/null)\" ;; esac\n"
	   << "    elif [ \"$COMP_CWORD\" -eq 4 ] && [ \"$sub\" = \"config\" ]; then\n"
	   << "      words=\"" << join(spec.config_subcommands) << "\"\n"
	   << "    fi\n"
	   << "  elif [ \"$COMP_CWORD\" -eq 2 ]; then\n"
	   << "    case \" " << kVersionCommands << " \" in *\" $cmd \"*) words=\"$(vb __complete versions 2>/dev/null)\" ;; esac\n"
	   << "    [ \"$cmd\" = \"completions\" ] && words=\"bash zsh fish powershell\"\n"
	   << "  fi\n"
	   << "  COMPREPLY=( $(compgen -W \"$words\" -- \"$cur\") )\n"
	   << "}\n"
	   << "complete -F _vb vb\n";
	return os.str();
}

std::string zsh(const CompletionSpec &spec) {
	std::ostringstream os;
	os << "#compdef vb\n# zsh completion for vb -- save as _vb in a directory on $fpath\n"
	   << "_vb() {\n"
	   << "  local -a cmds subs cfgs\n"
	   << "  cmds=(";
	for (const auto &[name, summary] : spec.commands) {
		os << "'" << name << ":" << escape_single(summary) << "' ";
	}
	os << ")\n"
	   << "  subs=(" << join(spec.server_subcommands) << ")\n"
	   << "  cfgs=(" << join(spec.config_subcommands) << ")\n"
	   << "  if [[ ${words[CURRENT-1]} == --version ]]; then\n"
	   << "    compadd -- ${(f)\"$(vb __complete versions 2>/dev/null)\"}\n"
	   << "  elif (( CURRENT == 2 )); then\n"
	   << "    _describe 'command' cmds\n"
	   << "  elif [[ ${words[2]} == server ]]; then\n"
	   << "    if (( CURRENT == 3 )); then compadd -- $subs\n"
	   << "    elif (( CURRENT == 4 )) && [[ \" " << kInstanceSubcommands << " \" == *\" ${words[3]} \"* ]]; then\n"
	   << "      compadd -- ${(f)\"$(vb __complete instances 2>/dev/null)\"}\n"
	   << "    elif (( CURRENT == 5 )) && [[ ${words[3]} == config ]]; then compadd -- $cfgs\n"
	   << "    fi\n"
	   << "  elif (( CURRENT == 3 )); then\n"
	   << "    if [[ \" " << kVersionCommands << " \" == *\" ${words[2]} \"* ]]; then\n"
	   << "      compadd -- ${(f)\"$(vb __complete versions 2>/dev/null)\"}\n"
	   << "    elif [[ ${words[2]} == completions ]]; then compadd bash zsh fish powershell\n"
	   << "    fi\n"
	   << "  fi\n"
	   << "}\n"
	   << "_vb \"$@\"\n";
	return os.str();
}

std::string fish(const CompletionSpec &spec) {
	std::ostringstream os;
	os << "# fish completion for vb -- save as ~/.config/fish/completions/vb.fish\n"
	   << "complete -c vb -f\n";
	for (const auto &[name, summary] : spec.commands) {
		os << "complete -c vb -n '__fish_use_subcommand' -a '" << name << "' -d '"
		   << escape_single(summary) << "'\n";
	}
	for (const std::string &sub : spec.server_subcommands) {
		os << "complete -c vb -n '__fish_seen_subcommand_from server; and not __fish_seen_subcommand_from "
		   << join(spec.server_subcommands) << "' -a '" << sub << "'\n";
	}
	os << "complete -c vb -n '__fish_seen_subcommand_from " << kInstanceSubcommands
	   << "; and __fish_seen_subcommand_from server' -a '(vb __complete instances 2>/dev/null)'\n"
	   << "complete -c vb -n '__fish_seen_subcommand_from " << kVersionCommands
	   << "' -a '(vb __complete versions 2>/dev/null)'\n"
	   << "complete -c vb -l version -x -a '(vb __complete versions 2>/dev/null)'\n"
	   << "complete -c vb -n '__fish_seen_subcommand_from completions' -a 'bash zsh fish powershell'\n";
	return os.str();
}

std::string powershell(const CompletionSpec &spec) {
	const auto quoted = [](const std::vector<std::string> &v) {
		std::string out;
		for (const std::string &s : v) {
			out += (out.empty() ? "" : ",") + std::string("'") + s + "'";
		}
		return out;
	};
	std::vector<std::string> n;
	for (const auto &[name, summary] : spec.commands) {
		n.push_back(name);
	}
	std::ostringstream os;
	os << "# PowerShell completion for vb -- add to your $PROFILE\n"
	   << "Register-ArgumentCompleter -Native -CommandName vb -ScriptBlock {\n"
	   << "  param($wordToComplete, $commandAst, $cursorPosition)\n"
	   << "  $w = @($commandAst.CommandElements | ForEach-Object { $_.ToString() })\n"
	   << "  if ($wordToComplete -eq '') { $w += '' }\n"
	   << "  $i = $w.Count - 1\n"
	   << "  $words = @()\n"
	   << "  if ($i -ge 1 -and $w[$i-1] -eq '--version') { $words = @(vb __complete versions 2>$null) }\n"
	   << "  elseif ($i -eq 1) { $words = @(" << quoted(n) << ") }\n"
	   << "  elseif ($w[1] -eq 'server') {\n"
	   << "    if ($i -eq 2) { $words = @(" << quoted(spec.server_subcommands) << ") }\n"
	   << "    elseif ($i -eq 3 -and @('start','stop','restart','status','logs','rm','config') -contains $w[2]) { $words = @(vb __complete instances 2>$null) }\n"
	   << "    elseif ($i -eq 4 -and $w[2] -eq 'config') { $words = @(" << quoted(spec.config_subcommands) << ") }\n"
	   << "  }\n"
	   << "  elseif ($i -eq 2 -and @('use','uninstall','unlink','which') -contains $w[1]) { $words = @(vb __complete versions 2>$null) }\n"
	   << "  elseif ($i -eq 2 -and $w[1] -eq 'completions') { $words = @('bash','zsh','fish','powershell') }\n"
	   << "  $words | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n"
	   << "    [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)\n"
	   << "  }\n"
	   << "}\n";
	return os.str();
}

} // namespace

std::string generate_completions(const std::string &shell, const CompletionSpec &spec) {
	if (shell == "bash")
		return bash(spec);
	if (shell == "zsh")
		return zsh(spec);
	if (shell == "fish")
		return fish(spec);
	if (shell == "powershell" || shell == "pwsh")
		return powershell(spec);
	return {};
}

} // namespace vb::cli

# vb/ecs — generic system-runner scaffolding (spec §6/§7.2). Compiled into
# vb_core; components themselves stay header-only in inc/vb/ecs/components.hpp.

target_sources(vb_core PRIVATE
  system_runner.cpp
)

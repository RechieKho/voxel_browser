# vb/physics — shared player movement + swept-AABB voxel collision (spec §7.3/§12).
# One implementation for the server VoxelCollisionSystem and client prediction.
# Compiled into vb_core.

target_sources(vb_core PRIVATE
  movement.cpp
)

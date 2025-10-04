#pragma once

// Chunk size config - change these constants project-wide if needed.
constexpr int32 CHUNK_SIZE_X = 16;
constexpr int32 CHUNK_SIZE_Y = 128; // vertical axis
constexpr int32 CHUNK_SIZE_Z = 16;
constexpr int32 CHUNK_VOLUME = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z;

constexpr int32 DEFAULT_WORLD_SEED = 1337;
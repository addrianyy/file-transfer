#include "Hasher.hpp"

#include "xxhash/xxhash.h"

#include <base/Log.hpp>

static XXH3_state_t* to_state(void* state) {
  return reinterpret_cast<XXH3_state_t*>(state);
}

Hasher::Hasher() {
  state = XXH3_createState();
  reset();
}
Hasher::~Hasher() {
  XXH3_freeState(to_state(state));
}

void Hasher::reset() {
  XXH3_64bits_reset(to_state(state));
}

void Hasher::feed(std::span<const uint8_t> bytes) {
  XXH3_64bits_update(to_state(state), bytes.data(), bytes.size());
}

uint64_t Hasher::finalize() {
  return XXH3_64bits_digest(to_state(state));
}
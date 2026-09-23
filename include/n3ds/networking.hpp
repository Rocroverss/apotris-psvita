#pragma once

namespace n3ds::networking {

/// May be called from the main thread more than once. No-op if networking
/// thread is already running.
void start();
void stop();
void process();
void process_wait_completion();

} // namespace n3ds::networking

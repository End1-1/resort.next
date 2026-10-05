#pragma once

#include <functional>

// Blocks in QCoreApplication::exec(). Calls onReady after the listeners are bound.
int runApplication(int argc, char **argv, const std::function<void()> &onReady = {});

// Safe to call from another thread (Windows service control handler).
void requestApplicationQuit();

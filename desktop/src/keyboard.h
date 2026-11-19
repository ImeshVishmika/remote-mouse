#pragma once

#include <cstdint>
#include <string>

#include "protocol.h"

// Windows keyboard injection. Text is sent as UTF-16 Unicode input, so it does
// not depend on the PC's active keyboard layout.
class KeyboardInjector {
public:
    explicit KeyboardInjector(bool dryRun);

    void typeText(const std::string& utf8);
    void pressKey(mctl::KeyCode key);

private:
    bool dryRun_;
};

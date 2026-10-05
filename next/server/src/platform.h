#pragma once

// Linux: foreground process (systemd Type=simple).
// Windows: HotelApi service, unless --console / not started by the SCM.
int platformMain(int argc, char **argv);

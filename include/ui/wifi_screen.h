#pragma once

/**
 * On-device Wi-Fi settings (LVGL): network list, password entry, connection
 * status. Drive it from loop(): open*, then call update() and run LVGL while
 * isOpen() is true. It closes itself once connected (or on Back).
 */
namespace ui::wifi_screen {

/**
 * Show the network list and start a scan.
 * @param cancelable show a Back button that returns to the radar
 */
void openScan(bool cancelable);

/** Connect to the saved network and show its progress (boot path). */
void openConnecting(bool cancelable);

bool isOpen();

/** Poll Wi-Fi state and refresh the screen. Call every loop while open. */
void update();

}  // namespace ui::wifi_screen

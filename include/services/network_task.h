#pragma once

/**
 * The network task (core 0): one-time IP location lookup, ADS-B polling,
 * route lookups for the selected plane and address searches. HTTP never runs
 * on the UI loop, and one connection at a time keeps peak RAM down.
 */
namespace services::network_task {

/** Create the task. Call once at boot, after the services it uses. */
void start();

/** Wake the task now (a route lookup or address search is waiting). */
void wake();

/** Fetch ADS-B right away, e.g. after the radar location changed. */
void fetchNow();

/**
 * Log heap state to serial: free bytes, largest free block (TLS needs about
 * 40 KB, partly contiguous), lowest free since boot, and the network task's
 * unused stack. `what` names the moment ("tick", "adsb", "route", ...).
 */
void logHeap(const char* what);

}  // namespace services::network_task

#pragma once

/**
 * Radar projection maths shared by the aircraft and runway layers.
 * Screen coordinates are relative to the radar area's top-left corner.
 */
namespace ui::radar {

/** Flat-earth offset (km) of lat/lon from the radar center; north = +dy. */
void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km);

/** Flat lat/lon as x/y at the current range: 1° ≈ 111 km, north = up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y);

int distSqFromCenter(int x, int y);

/** Pull (x1, y1) back along the segment from (x0, y0) until inside the outer ring. */
void clipPointToOuterRing(int x0, int y0, int* x1, int* y1);

/** Move (x, y) radially onto the outer ring if it lies outside. */
void clipPointOntoOuterRing(int* x, int* y);

/** True if the segment touches the outer-ring disc. */
bool segmentIntersectsDisc(int x0, int y0, int x1, int y1);

}  // namespace ui::radar

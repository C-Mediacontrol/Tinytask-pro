#ifndef TTP_VISION_H
#define TTP_VISION_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Euclidean Distance calculation */
double ttp_calc_euclidean_dist(LONG x1, LONG y1, LONG x2, LONG y2);

/* Pick candidate index that has minimum Euclidean distance to (origX, origY). Returns -1 if count <= 0 */
int ttp_pick_nearest_candidate(LONG origX, LONG origY, const POINT* candidates, int count);

/* Adaptive edge detection & button cropping:
 * Captures 256x256 ROI centered at (clickX, clickY) from hdcSrc.
 * Computes 3x3 Sobel gradients (horizontal & vertical).
 * Scans outwards in 4 directions to detect prominent contrast boundaries (button edges).
 * Fills outRect with bounding box (clamped between 20x15 and 220x90).
 * Allocates and returns a standard 24bpp uncompressed BMP memory buffer in *outBmp, size in *outBmpSize.
 */
BOOL ttp_adaptive_crop_button(HDC hdcSrc, LONG clickX, LONG clickY, RECT* outRect, BYTE** outBmp, DWORD* outBmpSize);

/* Pure-C Normalized Cross-Correlation (NCC) template matcher:
 * Takes screen DC (or bitmap) and a template BMP buffer.
 * Performs fast sliding-window NCC match.
 * If best peak score >= minScore, fills outMatchPos with center (x, y) and returns TRUE.
 */
BOOL ttp_match_template_ncc(HDC hdcScreen, int screenW, int screenH, const BYTE* bmpPattern, DWORD bmpSize, double minScore, POINT* outMatchPos, double* outScore);

/* UI Automation / Accessible text locator helper:
 * Finds bounding centers of all controls matching targetText in foreground / desktop window.
 * Returns count of found elements (up to maxCount).
 */
int ttp_find_elements_by_text(const char* targetText, POINT* outCenters, int maxCount);

void ttp_free_bmp_buffer(BYTE* bmpBuffer);

#ifdef __cplusplus
}
#endif

#endif /* TTP_VISION_H */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Haiku: Xlibe (the Xlib implementation on top of the Be API) has no RENDER
 * extension and there is no libXrender for it. Cairo's xlib backend and the
 * gfx code already cope with an X server that lacks RENDER, so these stubs
 * behave exactly like libXrender talking to such a server: the extension is
 * reported absent, every format lookup fails and every request is a no-op.
 */

#include <X11/Xlib.h>
#include <X11/extensions/Xrender.h>

Bool XRenderQueryExtension(Display* dpy, int* event_basep, int* error_basep)
{ return False; }

Status XRenderQueryVersion(Display* dpy, int* major_versionp,
                           int* minor_versionp)
{
  if (major_versionp) *major_versionp = -1;
  if (minor_versionp) *minor_versionp = -1;
  return 0;
}

Status XRenderQueryFormats(Display* dpy) { return 0; }
int XRenderQuerySubpixelOrder(Display* dpy, int screen)
{ return SubPixelUnknown; }
Bool XRenderSetSubpixelOrder(Display* dpy, int screen, int subpixel)
{ return False; }

XRenderPictFormat* XRenderFindVisualFormat(Display* dpy, _Xconst Visual* visual)
{ return NULL; }
XRenderPictFormat* XRenderFindFormat(Display* dpy, unsigned long mask,
                                     _Xconst XRenderPictFormat* templ,
                                     int count)
{ return NULL; }
XRenderPictFormat* XRenderFindStandardFormat(Display* dpy, int format)
{ return NULL; }
XIndexValue* XRenderQueryPictIndexValues(Display* dpy,
                                         _Xconst XRenderPictFormat* format,
                                         int* num)
{ if (num) *num = 0; return NULL; }

Picture XRenderCreatePicture(Display* dpy, Drawable drawable,
                             _Xconst XRenderPictFormat* format,
                             unsigned long valuemask,
                             _Xconst XRenderPictureAttributes* attributes)
{ return 0; }
void XRenderChangePicture(Display* dpy, Picture picture,
                          unsigned long valuemask,
                          _Xconst XRenderPictureAttributes* attributes) {}
void XRenderSetPictureClipRectangles(Display* dpy, Picture picture,
                                     int xOrigin, int yOrigin,
                                     _Xconst XRectangle* rects, int n) {}
void XRenderSetPictureClipRegion(Display* dpy, Picture picture, Region r) {}
void XRenderSetPictureTransform(Display* dpy, Picture picture,
                                XTransform* transform) {}
void XRenderFreePicture(Display* dpy, Picture picture) {}
void XRenderComposite(Display* dpy, int op, Picture src, Picture mask,
                      Picture dst, int src_x, int src_y, int mask_x,
                      int mask_y, int dst_x, int dst_y, unsigned int width,
                      unsigned int height) {}

GlyphSet XRenderCreateGlyphSet(Display* dpy, _Xconst XRenderPictFormat* format)
{ return 0; }
GlyphSet XRenderReferenceGlyphSet(Display* dpy, GlyphSet existing)
{ return 0; }
void XRenderFreeGlyphSet(Display* dpy, GlyphSet glyphset) {}
void XRenderAddGlyphs(Display* dpy, GlyphSet glyphset, _Xconst Glyph* gids,
                      _Xconst XGlyphInfo* glyphs, int nglyphs,
                      _Xconst char* images, int nbyte_images) {}
void XRenderFreeGlyphs(Display* dpy, GlyphSet glyphset, _Xconst Glyph* gids,
                       int nglyphs) {}

void XRenderCompositeString8(Display* dpy, int op, Picture src, Picture dst,
                             _Xconst XRenderPictFormat* maskFormat,
                             GlyphSet glyphset, int xSrc, int ySrc, int xDst,
                             int yDst, _Xconst char* string, int nchar) {}
void XRenderCompositeString16(Display* dpy, int op, Picture src, Picture dst,
                              _Xconst XRenderPictFormat* maskFormat,
                              GlyphSet glyphset, int xSrc, int ySrc, int xDst,
                              int yDst, _Xconst unsigned short* string,
                              int nchar) {}
void XRenderCompositeString32(Display* dpy, int op, Picture src, Picture dst,
                              _Xconst XRenderPictFormat* maskFormat,
                              GlyphSet glyphset, int xSrc, int ySrc, int xDst,
                              int yDst, _Xconst unsigned int* string,
                              int nchar) {}
void XRenderCompositeText8(Display* dpy, int op, Picture src, Picture dst,
                           _Xconst XRenderPictFormat* maskFormat, int xSrc,
                           int ySrc, int xDst, int yDst,
                           _Xconst XGlyphElt8* elts, int nelt) {}
void XRenderCompositeText16(Display* dpy, int op, Picture src, Picture dst,
                            _Xconst XRenderPictFormat* maskFormat, int xSrc,
                            int ySrc, int xDst, int yDst,
                            _Xconst XGlyphElt16* elts, int nelt) {}
void XRenderCompositeText32(Display* dpy, int op, Picture src, Picture dst,
                            _Xconst XRenderPictFormat* maskFormat, int xSrc,
                            int ySrc, int xDst, int yDst,
                            _Xconst XGlyphElt32* elts, int nelt) {}

void XRenderFillRectangle(Display* dpy, int op, Picture dst,
                          _Xconst XRenderColor* color, int x, int y,
                          unsigned int width, unsigned int height) {}
void XRenderFillRectangles(Display* dpy, int op, Picture dst,
                           _Xconst XRenderColor* color,
                           _Xconst XRectangle* rectangles, int n_rects) {}
void XRenderCompositeTrapezoids(Display* dpy, int op, Picture src, Picture dst,
                                _Xconst XRenderPictFormat* maskFormat,
                                int xSrc, int ySrc, _Xconst XTrapezoid* traps,
                                int ntrap) {}
void XRenderCompositeTriangles(Display* dpy, int op, Picture src, Picture dst,
                               _Xconst XRenderPictFormat* maskFormat,
                               int xSrc, int ySrc,
                               _Xconst XTriangle* triangles, int ntriangle) {}
void XRenderCompositeTriStrip(Display* dpy, int op, Picture src, Picture dst,
                              _Xconst XRenderPictFormat* maskFormat, int xSrc,
                              int ySrc, _Xconst XPointFixed* points,
                              int npoint) {}
void XRenderCompositeTriFan(Display* dpy, int op, Picture src, Picture dst,
                            _Xconst XRenderPictFormat* maskFormat, int xSrc,
                            int ySrc, _Xconst XPointFixed* points,
                            int npoint) {}
void XRenderCompositeDoublePoly(Display* dpy, int op, Picture src, Picture dst,
                                _Xconst XRenderPictFormat* maskFormat,
                                int xSrc, int ySrc, int xDst, int yDst,
                                _Xconst XPointDouble* fpoints, int npoints,
                                int winding) {}

Status XRenderParseColor(Display* dpy, char* spec, XRenderColor* def)
{ return 0; }
Cursor XRenderCreateCursor(Display* dpy, Picture source, unsigned int x,
                           unsigned int y)
{ return 0; }
XFilters* XRenderQueryFilters(Display* dpy, Drawable drawable)
{ return NULL; }
void XRenderSetPictureFilter(Display* dpy, Picture picture,
                             const char* filter, XFixed* params,
                             int nparams) {}
Cursor XRenderCreateAnimCursor(Display* dpy, int ncursor,
                               XAnimCursor* cursors)
{ return 0; }
void XRenderAddTraps(Display* dpy, Picture picture, int xOff, int yOff,
                     _Xconst XTrap* traps, int ntrap) {}

Picture XRenderCreateSolidFill(Display* dpy, const XRenderColor* color)
{ return 0; }
Picture XRenderCreateLinearGradient(Display* dpy,
                                    const XLinearGradient* gradient,
                                    const XFixed* stops,
                                    const XRenderColor* colors, int nstops)
{ return 0; }
Picture XRenderCreateRadialGradient(Display* dpy,
                                    const XRadialGradient* gradient,
                                    const XFixed* stops,
                                    const XRenderColor* colors, int nstops)
{ return 0; }
Picture XRenderCreateConicalGradient(Display* dpy,
                                     const XConicalGradient* gradient,
                                     const XFixed* stops,
                                     const XRenderColor* colors, int nstops)
{ return 0; }

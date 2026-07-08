#ifndef UIPIXMAP_H
#define UIPIXMAP_H

#include "uidefines.h"
#include "uicommon.h"
#ifdef _WIN32
# include <Windows.h>
# include <gdiplus.h>
  typedef Gdiplus::Bitmap PlatformBitmap;
#else
  typedef GdkPixbuf PlatformBitmap;
#endif

class DECL_VISUALUI UIPixmap
{
public:
    UIPixmap();
    UIPixmap(const UIPixmap &other);
    UIPixmap(UIPixmap &&other) noexcept;
    UIPixmap(const tstring &path);
    ~UIPixmap();

    UIPixmap& operator=(const UIPixmap &other);
    UIPixmap& operator=(UIPixmap &&other) noexcept;

    void reset();
    bool isValid() const noexcept;
    Size imageSize() const noexcept;
    UIPixmap scaled(int width, int height) const;

    static UIPixmap fromRawData(const uint8_t* data, int width, int height, bool hasAlpha = true);

protected:
    friend class UIconHandler;
    PlatformBitmap *m_hBmp;
};

#endif // UIPIXMAP_H

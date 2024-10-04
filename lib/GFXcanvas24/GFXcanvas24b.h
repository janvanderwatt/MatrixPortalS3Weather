#include "Adafruit_GFX.h"

///  A GFX 16-bit canvas context for graphics
struct RGB24 {
    uint8_t r, g, b;
};

class GFXcanvas24b : public Adafruit_GFX {
public:
  GFXcanvas24b(uint16_t w, uint16_t h);
  ~GFXcanvas24b(void);
  void setBrightness(uint8_t brightness);
  void drawPixel(int16_t x, int16_t y, RGB24 color);
  void fillScreen(RGB24 color);
  void byteSwap(void);
  void drawFastVLine(int16_t x, int16_t y, int16_t h, RGB24 color);
  void drawFastHLine(int16_t x, int16_t y, int16_t w, RGB24 color);
  void adjustColorBrightness(RGB24 &c);
  RGB24 getPixel(int16_t x, int16_t y) const;
  /**********************************************************************/
  /*!
    @brief    Get a pointer to the internal buffer memory
    @returns  A pointer to the allocated buffer
  */
  /**********************************************************************/
  uint8_t *getBuffer(void) const { return buffer; }

protected:
  RGB24 getRawPixel(int16_t x, int16_t y) const;
  void drawFastRawVLine(int16_t x, int16_t y, int16_t h, RGB24 color);
  void drawFastRawHLine(int16_t x, int16_t y, int16_t w, RGB24 color);
  uint8_t *buffer; ///< Raster data: no longer private, allow subclass access
  uint16_t brightness=255;  ////< Store 8-bit brightness as 16-bit so that multiplications with it are 16-bit by default
};
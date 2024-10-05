#include "GFXcanvas24b.h"

/**************************************************************************/
/*!
   @brief    Instatiate a GFX 24-bit canvas context for graphics
   @param    w   Display width, in pixels
   @param    h   Display height, in pixels
*/
/**************************************************************************/
GFXcanvas24b::GFXcanvas24b(uint16_t w, uint16_t h) : Adafruit_GFX(w, h) {
    uint32_t bytes = w * h * 3;
    if ((buffer = (uint8_t *)malloc(bytes))) {
        memset(buffer, 0, bytes);
    }
}

/**************************************************************************/
/*!
   @brief    Delete the canvas, free memory
*/
/**************************************************************************/
GFXcanvas24b::~GFXcanvas24b(void) {
    if (buffer)
        free(buffer);
}

/**************************************************************************/
/*!
    @brief  Set the brigtness applied to all colours
    @param  brightness   8-bit brighness
*/
/**************************************************************************/
void GFXcanvas24b::setBrightness(uint8_t brightness) {
    this->brightness = brightness + 1;  ///< Add 1 so that full brightness doesn't scale down; brightness of 1 is still going to be 0.
}

/**************************************************************************/
/*!
    @brief  Adjust the colour of an incoming pixel to the canvas' brightness
    @param  color  reference to 24-bit RGB Color that is modified on return
*/
/**************************************************************************/
void GFXcanvas24b::adjustColorBrightness(RGB24 &color) {
    color.r = (color.r * this->brightness) >> 8;
    color.g = (color.g * this->brightness) >> 8;
    color.b = (color.b * this->brightness) >> 8;
}

/**************************************************************************/
/*!
    @brief  Draw a pixel to the canvas framebuffer
    @param  x   x coordinate
    @param  y   y coordinate
    @param  color  16-bit 5-6-5 Color to draw line with
*/
/**************************************************************************/
void GFXcanvas24b::drawPixel(int16_t x, int16_t y, uint16_t color) {
    RGB24 c;
    c.r = color >> 11;
    c.g = (color >> 5) & 0x3F;
    c.b = color & 0x1F;
    drawPixel(x, y, c);
}

/**************************************************************************/
/*!
    @brief  Draw a pixel to the canvas framebuffer
    @param  x   x coordinate
    @param  y   y coordinate
    @param  color  24-bit RGB Color to draw line with
*/
/**************************************************************************/
void GFXcanvas24b::drawPixel(int16_t x, int16_t y, RGB24 color) {
    if (buffer) {
        if ((x < 0) || (y < 0) || (x >= _width) || (y >= _height))
            return;

        int16_t t;
        switch (rotation) {
        case 1:
            t = x;
            x = WIDTH - 1 - y;
            y = t;
            break;
        case 2:
            x = WIDTH - 1 - x;
            y = HEIGHT - 1 - y;
            break;
        case 3:
            t = x;
            x = y;
            y = HEIGHT - 1 - t;
            break;
        }

        t = x + y * WIDTH;

        adjustColorBrightness(color);

        buffer[t + 0] = color.r;
        buffer[t + 1] = color.g;
        buffer[t + 2] = color.b;
    }
}

/**********************************************************************/
/*!
        @brief    Get the pixel color value at a given coordinate
        @param    x   x coordinate
        @param    y   y coordinate
        @returns  The desired pixel's 24-bit RGB color value
*/
/**********************************************************************/
RGB24 GFXcanvas24b::getPixel(int16_t x, int16_t y) const {
    int16_t t;
    switch (rotation) {
    case 1:
        t = x;
        x = WIDTH - 1 - y;
        y = t;
        break;
    case 2:
        x = WIDTH - 1 - x;
        y = HEIGHT - 1 - y;
        break;
    case 3:
        t = x;
        x = y;
        y = HEIGHT - 1 - t;
        break;
    }
    return getRawPixel(x, y);
}

/**********************************************************************/
/*!
        @brief    Get the pixel color value at a given, unrotated coordinate.
              This method is intended for hardware drivers to get pixel value
              in physical coordinates.
        @param    x   x coordinate
        @param    y   y coordinate
        @returns  The desired pixel's 24-bit RGB color value
*/
/**********************************************************************/
RGB24 GFXcanvas24b::getRawPixel(int16_t x, int16_t y) const {
    if ((x < 0) || (y < 0) || (x >= WIDTH) || (y >= HEIGHT))
        return {0};
    if (buffer) {
        RGB24 c;
        uint16_t t = x + y * WIDTH;
        c.r = buffer[t + 0];
        c.g = buffer[t + 1];
        c.b = buffer[t + 2];
        return c;
    }
    return {0};
}

/**************************************************************************/
/*!
    @brief  Fill the framebuffer completely with one color
    @param  color 24-bit RGB Color to draw line with
*/
/**************************************************************************/
void GFXcanvas24b::fillScreen(RGB24 color) {
    adjustColorBrightness(color);
    if (buffer) {
        if (color.r == color.g && color.g == color.b) {
            memset(buffer, color.r, WIDTH * HEIGHT * 3);
        } else {
            uint32_t i, pixels = WIDTH * HEIGHT * 3;
            for (i = 0; i < pixels; i += 3) {
                buffer[i + 0] = color.r;
                buffer[i + 1] = color.g;
                buffer[i + 2] = color.b;
            }
        }
    }
}

/**************************************************************************/
/*!
   @brief    Speed optimized vertical line drawing
   @param    x   Line horizontal start point
   @param    y   Line vertical start point
   @param    h   length of vertical line to be drawn, including first point
   @param    color   color 24-bit RGB Color to draw line with
*/
/**************************************************************************/
void GFXcanvas24b::drawFastVLine(int16_t x, int16_t y, int16_t h, RGB24 color) {
    if (h < 0) { // Convert negative heights to positive equivalent
        h *= -1;
        y -= h - 1;
        if (y < 0) {
            h += y;
            y = 0;
        }
    }

    // Edge rejection (no-draw if totally off canvas)
    if ((x < 0) || (x >= width()) || (y >= height()) || ((y + h - 1) < 0)) {
        return;
    }

    if (y < 0) { // Clip top
        h += y;
        y = 0;
    }
    if (y + h > height()) { // Clip bottom
        h = height() - y;
    }

    adjustColorBrightness(color);

    if (getRotation() == 0) {
        drawFastRawVLine(x, y, h, color);
    } else if (getRotation() == 1) {
        int16_t t = x;
        x = WIDTH - 1 - y;
        y = t;
        x -= h - 1;
        drawFastRawHLine(x, y, h, color);
    } else if (getRotation() == 2) {
        x = WIDTH - 1 - x;
        y = HEIGHT - 1 - y;

        y -= h - 1;
        drawFastRawVLine(x, y, h, color);
    } else if (getRotation() == 3) {
        int16_t t = x;
        x = y;
        y = HEIGHT - 1 - t;
        drawFastRawHLine(x, y, h, color);
    }
}

/**************************************************************************/
/*!
   @brief  Speed optimized horizontal line drawing
   @param  x      Line horizontal start point
   @param  y      Line vertical start point
   @param  w      Length of horizontal line to be drawn, including 1st point
   @param  color  color 24-bit RGB Color to draw line with
*/
/**************************************************************************/
void GFXcanvas24b::drawFastHLine(int16_t x, int16_t y, int16_t w, RGB24 color) {
    if (w < 0) { // Convert negative widths to positive equivalent
        w *= -1;
        x -= w - 1;
        if (x < 0) {
            w += x;
            x = 0;
        }
    }

    // Edge rejection (no-draw if totally off canvas)
    if ((y < 0) || (y >= height()) || (x >= width()) || ((x + w - 1) < 0)) {
        return;
    }

    if (x < 0) { // Clip left
        w += x;
        x = 0;
    }
    if (x + w >= width()) { // Clip right
        w = width() - x;
    }

    adjustColorBrightness(color);

    if (getRotation() == 0) {
        drawFastRawHLine(x, y, w, color);
    } else if (getRotation() == 1) {
        int16_t t = x;
        x = WIDTH - 1 - y;
        y = t;
        drawFastRawVLine(x, y, w, color);
    } else if (getRotation() == 2) {
        x = WIDTH - 1 - x;
        y = HEIGHT - 1 - y;

        x -= w - 1;
        drawFastRawHLine(x, y, w, color);
    } else if (getRotation() == 3) {
        int16_t t = x;
        x = y;
        y = HEIGHT - 1 - t;
        y -= w - 1;
        drawFastRawVLine(x, y, w, color);
    }
}

/**************************************************************************/
/*!
   @brief    Speed optimized vertical line drawing into the raw canvas buffer
   @param    x   Line horizontal start point
   @param    y   Line vertical start point
   @param    h   length of vertical line to be drawn, including first point
   @param    color   color 24-bit RGB Color to draw line with
*/
/**************************************************************************/
void GFXcanvas24b::drawFastRawVLine(int16_t x, int16_t y, int16_t h, RGB24 color) {
    // x & y already in raw (rotation 0) coordinates, no need to transform.
    uint32_t buffer_index = (y * WIDTH + x) * 3;
    for (uint16_t i = 0; i < h; i++) {
        buffer[buffer_index + 0] = color.r;
        buffer[buffer_index + 1] = color.g;
        buffer[buffer_index + 2] = color.b;
        buffer_index += WIDTH * 3;
    }
}

/**************************************************************************/
/*!
   @brief    Speed optimized horizontal line drawing into the raw canvas buffer
   @param    x   Line horizontal start point
   @param    y   Line vertical start point
   @param    w   length of horizontal line to be drawn, including first point
   @param    color   color 24-bit RGB Color to draw line with
*/
/**************************************************************************/
void GFXcanvas24b::drawFastRawHLine(int16_t x, int16_t y, int16_t w, RGB24 color) {
    // x & y already in raw (rotation 0) coordinates, no need to transform.
    uint32_t buffer_index = (y * WIDTH + x) * 3;
    for (uint32_t i = buffer_index; i < buffer_index + w; i += 3) {
        buffer[i + 0] = color.r;
        buffer[i + 1] = color.g;
        buffer[i + 2] = color.b;
    }
}

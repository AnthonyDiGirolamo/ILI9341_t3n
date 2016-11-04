// https://github.com/PaulStoffregen/ILI9341_t3n
// http://forum.pjrc.com/threads/26305-Highly-optimized-ILI9341-(320x240-TFT-color-display)-library

/***************************************************
  This is our library for the Adafruit ILI9341 Breakout and Shield
  ----> http://www.adafruit.com/products/1651

  Check out the links above for our tutorials and wiring diagrams
  These displays use SPI to communicate, 4 or 5 pins are required to
  interface (RST is optional)
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/
// <SoftEgg>

//Additional graphics routines by Tim Trzepacz, SoftEgg LLC added December 2015
//(And then accidentally deleted and rewritten March 2016. Oops!)
//Gradient support 
//----------------
//		fillRectVGradient	- fills area with vertical gradient
//		fillRectHGradient	- fills area with horizontal gradient
//		fillScreenVGradient - fills screen with vertical gradient
// 	fillScreenHGradient - fills screen with horizontal gradient

//Additional Color Support
//------------------------
//		color565toRGB		- converts 565 format 16 bit color to RGB
//		color565toRGB14		- converts 16 bit 565 format color to 14 bit RGB (2 bits clear for math and sign)
//		RGB14tocolor565		- converts 14 bit RGB back to 16 bit 565 format color

//Low Memory Bitmap Support
//-------------------------
// 		writeRect8BPP - 	write 8 bit per pixel paletted bitmap
// 		writeRect4BPP - 	write 4 bit per pixel paletted bitmap
// 		writeRect2BPP - 	write 2 bit per pixel paletted bitmap
// 		writeRect1BPP - 	write 1 bit per pixel paletted bitmap

//TODO: transparent bitmap writing routines for sprites

//String Pixel Length support 
//---------------------------
//		strPixelLen			- gets pixel length of given ASCII string

// <\SoftEgg>

#include "ILI9341_t3n.h"
#include <SPIN.h>
#include <SPI.h>  

// Teensy 3.1 can only generate 30 MHz SPI when running at 120 MHz (overclock)

#define WIDTH  ILI9341_TFTWIDTH
#define HEIGHT ILI9341_TFTHEIGHT

// Constructor when using hardware ILI9241_KINETISK__pspi->  Faster, but must use SPI pins
// specific to each board type (e.g. 11,13 for Uno, 51,52 for Mega, etc.)
ILI9341_t3n::ILI9341_t3n(uint8_t cs, uint8_t dc, uint8_t rst, 
	uint8_t mosi, uint8_t sclk, uint8_t miso, SPINClass *pspin )
{
	_cs   = cs;
	_dc   = dc;
	_rst  = rst;
	_mosi = mosi;
	_sclk = sclk;
	_miso = miso;
	_width    = WIDTH;
	_height   = HEIGHT;
	_pspin	  = pspin;
	_pkinetisk_spi = _pspin->kinetisk_spi();

	rotation  = 0;
	cursor_y  = cursor_x    = 0;
	textsize  = 1;
	textcolor = textbgcolor = 0xFFFF;
	wrap      = true;
	font      = NULL;
	// Added to see how much impact actually using non hardware CS pin might be
    _cspinmask = 0;
    _csport = NULL;

    _pfbtft = NULL;	
    _use_fbtft = 0;						// Are we in frame buffer mode?


}

//=======================================================================
// Add optinal support for using frame buffer to speed up complex outputs
//=======================================================================
uint8_t ILI9341_t3n::useFrameBuffer(boolean b)		// use the frame buffer?  First call will allocate
{
	if (b) {
		// First see if we need to allocate buffer
		if (_pfbtft == NULL) {
			_pfbtft = (uint16_t *)malloc(ILI9341_TFTHEIGHT*ILI9341_TFTWIDTH*2);
			if (_pfbtft == NULL)
				return 0;	// failed 
			memset(_pfbtft, 0, ILI9341_TFTHEIGHT*ILI9341_TFTWIDTH*2);	
		}
		_use_fbtft = 1;
	} else 
		_use_fbtft = 0;

	return _use_fbtft;	

}

void ILI9341_t3n::freeFrameBuffer(void)						// explicit call to release the buffer
{
	if (_pfbtft != NULL) {
		free(_pfbtft);
		_pfbtft = NULL;
		_use_fbtft = 0;	// make sure the use is turned off
	}
}
void ILI9341_t3n::updateScreen(void)					// call to say update the screen now.
{
	// Not sure if better here to check flag or check existence of buffer.
	// Will go by buffer as maybe can do interesting things?
	if (_use_fbtft) {
		beginSPITransaction();
		setAddr(0, 0, _width-1, _height-1);
		writecommand_cont(ILI9341_RAMWR);

		// BUGBUG doing as one shot.  Not sure if should or not or do like
		// main code and break up into transactions...
		uint16_t *pfbtft_end = &_pfbtft[(ILI9341_TFTWIDTH*ILI9341_TFTHEIGHT)-1];	// setup 
		uint16_t *pftbft = _pfbtft;

		// Quick write out the data;
		while (pftbft < pfbtft_end) {
			writedata16_cont(*pftbft++);
		}
		writedata16_last(*pftbft);
		endSPITransaction();
	}

}			 

//=======================================================================


void ILI9341_t3n::setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	beginSPITransaction();
	setAddr(x0, y0, x1, y1);
	writecommand_last(ILI9341_RAMWR); // write to RAM
	endSPITransaction();
}

void ILI9341_t3n::pushColor(uint16_t color)
{
	beginSPITransaction();
	writedata16_last(color);
	endSPITransaction();
}

void ILI9341_t3n::drawPixel(int16_t x, int16_t y, uint16_t color) {

	if((x < 0) ||(x >= _width) || (y < 0) || (y >= _height)) return;

	if (_use_fbtft) {
		_pfbtft[y*_width + x] = color;

	} else {
		beginSPITransaction();
		setAddr(x, y, x, y);
		writecommand_cont(ILI9341_RAMWR);
		writedata16_last(color);
		endSPITransaction();
	}
}

void ILI9341_t3n::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
	// Rudimentary clipping
	if((x >= _width) || (x < 0) || (y >= _height)) return;
	if(y < 0) {	h += y; y = 0; 	}
	if((y+h-1) >= _height) h = _height-y;
	if (_use_fbtft) {
		uint16_t * pfbPixel = &_pfbtft[ y*_width + x];
		while (h--) {
			*pfbPixel = color;
			pfbPixel += _width;
		}
	} else {
		beginSPITransaction();
		setAddr(x, y, x, y+h-1);
		writecommand_cont(ILI9341_RAMWR);
		while (h-- > 1) {
			writedata16_cont(color);
		}
		writedata16_last(color);
		endSPITransaction();
	}
}

void ILI9341_t3n::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
	// Rudimentary clipping
	if((x >= _width) || (y >= _height) || (y < 0)) return;
	if(x < 0) {	w += x; x = 0; 	}
	if((x+w-1) >= _width)  w = _width-x;
	if (_use_fbtft) {
		if ((x&1) || (w&1)) {
			uint16_t * pfbPixel = &_pfbtft[ y*_width + x];
			while (w--) {
				*pfbPixel++ = color;
			}
		} else {
			// X is even and so is w, try 32 bit writes..
			uint32_t color32 = (color << 16) | color;
			uint32_t * pfbPixel = (uint32_t*)((uint16_t*)&_pfbtft[ y*_width + x]);
			while (w) {
				*pfbPixel++ = color32;
				w -= 2;
			}
		}
	} else {
		beginSPITransaction();
		setAddr(x, y, x+w-1, y);
		writecommand_cont(ILI9341_RAMWR);
		while (w-- > 1) {
			writedata16_cont(color);
		}
		writedata16_last(color);
		endSPITransaction();
	}
}

void ILI9341_t3n::fillScreen(uint16_t color)
{
	if (_use_fbtft) {
		// Speed up lifted from Franks DMA code... 
		uint32_t color32 = (color << 16) | color;

		uint32_t *pfbPixel = (uint32_t *)_pfbtft;
		uint32_t *pfbtft_end = (uint32_t *)((uint16_t *)&_pfbtft[(ILI9341_TFTWIDTH * ILI9341_TFTHEIGHT)]); // setup
		while (pfbPixel < pfbtft_end) {
			*pfbPixel++ = color32; *pfbPixel++ = color32; *pfbPixel++ = color32;*pfbPixel++ = color32;
			*pfbPixel++ = color32; *pfbPixel++ = color32; *pfbPixel++ = color32;*pfbPixel++ = color32;
			*pfbPixel++ = color32; *pfbPixel++ = color32; *pfbPixel++ = color32;*pfbPixel++ = color32;
			*pfbPixel++ = color32; *pfbPixel++ = color32; *pfbPixel++ = color32;*pfbPixel++ = color32;
			*pfbPixel++ = color32; *pfbPixel++ = color32; *pfbPixel++ = color32;*pfbPixel++ = color32;
			*pfbPixel++ = color32; *pfbPixel++ = color32; *pfbPixel++ = color32;*pfbPixel++ = color32;
			*pfbPixel++ = color32; *pfbPixel++ = color32; *pfbPixel++ = color32;*pfbPixel++ = color32;
			*pfbPixel++ = color32; *pfbPixel++ = color32; *pfbPixel++ = color32;*pfbPixel++ = color32;
		}

	} else {
		fillRect(0, 0, _width, _height, color);
	}
}

// fill a rectangle
void ILI9341_t3n::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
	// rudimentary clipping (drawChar w/big text requires this)
	if((x >= _width) || (y >= _height)) return;
	if(x < 0) {	w += x; x = 0; 	}
	if(y < 0) {	h += y; y = 0; 	}
	if((x + w - 1) >= _width)  w = _width  - x;
	if((y + h - 1) >= _height) h = _height - y;

	if (_use_fbtft) {
		if ((x&1) || (w&1)) {
			uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
			for (;h>0; h--) {
				uint16_t * pfbPixel = pfbPixel_row;
				for (int i = 0 ;i < w; i++) {
					*pfbPixel++ = color;
				}
				pfbPixel_row += _width;
			}
		} else {
			// Horizontal is even number so try 32 bit writes instead
			uint32_t color32 = (color << 16) | color;
			uint32_t * pfbPixel_row = (uint32_t *)((uint16_t*)&_pfbtft[ y*_width + x]);
			w = w/2;	// only iterate half the times
			for (;h>0; h--) {
				uint32_t * pfbPixel = pfbPixel_row;
				for (int i = 0 ;i < w; i++) {
					*pfbPixel++ = color32;
				}
				pfbPixel_row += (_width/2);
			}
		}
	} else {

		// TODO: this can result in a very long transaction time
		// should break this into multiple transactions, even though
		// it'll cost more overhead, so we don't stall other SPI libs
		beginSPITransaction();
		setAddr(x, y, x+w-1, y+h-1);
		writecommand_cont(ILI9341_RAMWR);
		for(y=h; y>0; y--) {
			for(x=w; x>1; x--) {
				writedata16_cont(color);
			}
			writedata16_last(color);
			if (y > 1 && (y & 1)) {
				endSPITransaction();
				beginSPITransaction();
			}
		}
		endSPITransaction();
	}
}

// fillRectVGradient	- fills area with vertical gradient
void ILI9341_t3n::fillRectVGradient(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color1, uint16_t color2)
{
	// rudimentary clipping (drawChar w/big text requires this)
	if((x >= _width) || (y >= _height)) return;
	if(x < 0) {	w += x; x = 0; 	}
	if(y < 0) {	h += y; y = 0; 	}
	if((x + w - 1) >= _width)  w = _width  - x;
	if((y + h - 1) >= _height) h = _height - y;
	
	int16_t r1, g1, b1, r2, g2, b2, dr, dg, db, r, g, b;
	color565toRGB14(color1,r1,g1,b1);
	color565toRGB14(color2,r2,g2,b2);
	dr=(r2-r1)/h; dg=(g2-g1)/h; db=(b2-b1)/h;
	r=r1;g=g1;b=b1;	

	if (_use_fbtft) {
		if ((x&1) || (w&1)) {
			uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
			for (;h>0; h--) {
				uint16_t color = RGB14tocolor565(r,g,b);
				uint16_t * pfbPixel = pfbPixel_row;
				for (int i = 0 ;i < w; i++) {
					*pfbPixel++ = color;
				}
				r+=dr;g+=dg; b+=db;
				pfbPixel_row += _width;
			}
		} else {
			// Horizontal is even number so try 32 bit writes instead
			uint32_t * pfbPixel_row = (uint32_t *)((uint16_t*)&_pfbtft[ y*_width + x]);
			w = w/2;	// only iterate half the times
			for (;h>0; h--) {
				uint32_t * pfbPixel = pfbPixel_row;
				uint16_t color = RGB14tocolor565(r,g,b);
				uint32_t color32 = (color << 16) | color;
				for (int i = 0 ;i < w; i++) {
					*pfbPixel++ = color32;
				}
				pfbPixel_row += (_width/2);
				r+=dr;g+=dg; b+=db;
			}
		}
	} else {		
		beginSPITransaction();
		setAddr(x, y, x+w-1, y+h-1);
		writecommand_cont(ILI9341_RAMWR);
		for(y=h; y>0; y--) {
			uint16_t color = RGB14tocolor565(r,g,b);

			for(x=w; x>1; x--) {
				writedata16_cont(color);
			}
			writedata16_last(color);
			if (y > 1 && (y & 1)) {
				endSPITransaction();
				beginSPITransaction();
			}
			r+=dr;g+=dg; b+=db;
		}
		endSPITransaction();
	}
}

// fillRectHGradient	- fills area with horizontal gradient
void ILI9341_t3n::fillRectHGradient(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color1, uint16_t color2)
{
	// rudimentary clipping (drawChar w/big text requires this)
	if((x >= _width) || (y >= _height)) return;
	if(x < 0) {	w += x; x = 0; 	}
	if(y < 0) {	h += y; y = 0; 	}
	if((x + w - 1) >= _width)  w = _width  - x;
	if((y + h - 1) >= _height) h = _height - y;
	
	int16_t r1, g1, b1, r2, g2, b2, dr, dg, db, r, g, b;
	uint16_t color;
	color565toRGB14(color1,r1,g1,b1);
	color565toRGB14(color2,r2,g2,b2);
	dr=(r2-r1)/h; dg=(g2-g1)/h; db=(b2-b1)/h;
	r=r1;g=g1;b=b1;	
	if (_use_fbtft) {
		uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
		for (;h>0; h--) {
			uint16_t * pfbPixel = pfbPixel_row;
			for (int i = 0 ;i < w; i++) {
				*pfbPixel++ = RGB14tocolor565(r,g,b);
				r+=dr;g+=dg; b+=db;
			}
			pfbPixel_row += _width;
			r=r1;g=g1;b=b1;
		}
	} else {
		beginSPITransaction();
		setAddr(x, y, x+w-1, y+h-1);
		writecommand_cont(ILI9341_RAMWR);
		for(y=h; y>0; y--) {
			for(x=w; x>1; x--) {
				color = RGB14tocolor565(r,g,b);
				writedata16_cont(color);
				r+=dr;g+=dg; b+=db;
			}
			color = RGB14tocolor565(r,g,b);
			writedata16_last(color);
			if (y > 1 && (y & 1)) {
				endSPITransaction();
				beginSPITransaction();
			}
			r=r1;g=g1;b=b1;
		}
		endSPITransaction();
	}
}

// fillScreenVGradient - fills screen with vertical gradient
void ILI9341_t3n::fillScreenVGradient(uint16_t color1, uint16_t color2)
{
	fillRectVGradient(0,0,_width,_height,color1,color2);
}

// fillScreenHGradient - fills screen with horizontal gradient
void ILI9341_t3n::fillScreenHGradient(uint16_t color1, uint16_t color2)
{
	fillRectHGradient(0,0,_width,_height,color1,color2);
}


#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_ML  0x10
#define MADCTL_RGB 0x00
#define MADCTL_BGR 0x08
#define MADCTL_MH  0x04

void ILI9341_t3n::setRotation(uint8_t m)
{
	rotation = m % 4; // can't be higher than 3
	beginSPITransaction();
	writecommand_cont(ILI9341_MADCTL);
	switch (rotation) {
	case 0:
		writedata8_last(MADCTL_MX | MADCTL_BGR);
		_width  = ILI9341_TFTWIDTH;
		_height = ILI9341_TFTHEIGHT;
		break;
	case 1:
		writedata8_last(MADCTL_MV | MADCTL_BGR);
		_width  = ILI9341_TFTHEIGHT;
		_height = ILI9341_TFTWIDTH;
		break;
	case 2:
		writedata8_last(MADCTL_MY | MADCTL_BGR);
		_width  = ILI9341_TFTWIDTH;
		_height = ILI9341_TFTHEIGHT;
		break;
	case 3:
		writedata8_last(MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR);
		_width  = ILI9341_TFTHEIGHT;
		_height = ILI9341_TFTWIDTH;
		break;
	}
	endSPITransaction();
	cursor_x = 0;
	cursor_y = 0;
}

void ILI9341_t3n::setScroll(uint16_t offset)
{
	beginSPITransaction();
	writecommand_cont(ILI9341_VSCRSADD);
	writedata16_last(offset);
	endSPITransaction();
}

void ILI9341_t3n::invertDisplay(boolean i)
{
	beginSPITransaction();
	writecommand_last(i ? ILI9341_INVON : ILI9341_INVOFF);
	endSPITransaction();
}










/*
uint8_t ILI9341_t3n::readdata(void)
{
  uint8_t r;
       // Try to work directly with SPI registers...
       // First wait until output queue is empty
        uint16_t wTimeout = 0xffff;
        while (((_pkinetisk_spi->SR) & (15 << 12)) && (--wTimeout)) ; // wait until empty
        
//       	_pkinetisk_spi->MCR |= SPI_MCR_CLR_RXF; // discard any received data
//		_pkinetisk_spi->SR = SPI_SR_TCF;
        
        // Transfer a 0 out... 
        writedata8_cont(0);   
        
        // Now wait until completed. 
        wTimeout = 0xffff;
        while (((_pkinetisk_spi->SR) & (15 << 12)) && (--wTimeout)) ; // wait until empty
        r = _pkinetisk_spi->POPR;  // get the received byte... should check for it first...
    return r;
}
 */

uint8_t ILI9341_t3n::readcommand8(uint8_t c, uint8_t index)
{
    uint16_t wTimeout = 0xffff;
    uint8_t r=0;

    beginSPITransaction();
	if (_pspin->sizeFIFO() >= 4) {
	    while (((_pkinetisk_spi->SR) & (15 << 12)) && (--wTimeout)) ; // wait until empty

	    // Make sure the last frame has been sent...
	    _pkinetisk_spi->SR = SPI_SR_TCF;   // dlear it out;
	    wTimeout = 0xffff;
	    while (!((_pkinetisk_spi->SR) & SPI_SR_TCF) && (--wTimeout)) ; // wait until it says the last frame completed

	    // clear out any current received bytes
	    wTimeout = 0x10;    // should not go more than 4...
	    while ((((_pkinetisk_spi->SR) >> 4) & 0xf) && (--wTimeout))  {
	        r = _pkinetisk_spi->POPR;
	    }

	    //writecommand(0xD9); // sekret command
		_pkinetisk_spi->PUSHR = 0xD9 | (pcs_command << 16) | SPI_PUSHR_CTAS(0) | SPI_PUSHR_CONT;
	//	while (((_pkinetisk_spi->SR) & (15 << 12)) > (3 << 12)) ; // wait if FIFO full

	    // writedata(0x10 + index);
		_pkinetisk_spi->PUSHR = (0x10 + index) | (pcs_data << 16) | SPI_PUSHR_CTAS(0);
	//	while (((_pkinetisk_spi->SR) & (15 << 12)) > (3 << 12)) ; // wait if FIFO full
	    // writecommand(c);
	   	_pkinetisk_spi->PUSHR = c | (pcs_command << 16) | SPI_PUSHR_CTAS(0) | SPI_PUSHR_CONT;
	//	while (((_pkinetisk_spi->SR) & (15 << 12)) > (3 << 12)) ; // wait if FIFO full

	    // readdata
		_pkinetisk_spi->PUSHR = 0 | (pcs_data << 16) | SPI_PUSHR_CTAS(0);
	//	while (((_pkinetisk_spi->SR) & (15 << 12)) > (3 << 12)) ; // wait if FIFO full

	    // Now wait until completed.
	    wTimeout = 0xffff;
	    while (((_pkinetisk_spi->SR) & (15 << 12)) && (--wTimeout)) ; // wait until empty

	    // Make sure the last frame has been sent...
	    _pkinetisk_spi->SR = SPI_SR_TCF;   // dlear it out;
	    wTimeout = 0xffff;
	    while (!((_pkinetisk_spi->SR) & SPI_SR_TCF) && (--wTimeout)) ; // wait until it says the last frame completed

	    wTimeout = 0x10;    // should not go more than 4...
	    // lets get all of the values on the FIFO
	    while ((((_pkinetisk_spi->SR) >> 4) & 0xf) && (--wTimeout))  {
	        r = _pkinetisk_spi->POPR;
	    }
	} else {
		while (((_pkinetisk_spi->SR) & (15 << 12)) && (--wTimeout)) ; // wait until empty

	    // Make sure the last frame has been sent...
	    _pkinetisk_spi->SR = SPI_SR_TCF;   // dlear it out;
	    wTimeout = 0xffff;
	    while (!((_pkinetisk_spi->SR) & SPI_SR_TCF) && (--wTimeout)) ; // wait until it says the last frame completed

	    // clear out any current received bytes
	    wTimeout = 0x10;    // should not go more than 4...
	    while ((((_pkinetisk_spi->SR) >> 4) & 0xf) && (--wTimeout))  {
	        r = _pkinetisk_spi->POPR;
	    }

	    //writecommand(0xD9); // sekret command
		_pkinetisk_spi->PUSHR = 0xD9 | (pcs_command << 16) | SPI_PUSHR_CTAS(0) | SPI_PUSHR_CONT;
		while (((_pkinetisk_spi->SR) & (15 << 12)) > (0 << 12)) ; // wait if FIFO full

	    // writedata(0x10 + index);
		_pkinetisk_spi->PUSHR = (0x10 + index) | (pcs_data << 16) | SPI_PUSHR_CTAS(0);
		while (((_pkinetisk_spi->SR) & (15 << 12)) > (0 << 12)) ; // wait if FIFO full

		// See if there are any return values to pop 
	    while (((_pkinetisk_spi->SR) >> 4) & 0xf)  {
	        r = _pkinetisk_spi->POPR;
	    }

	    // writecommand(c);
	   	_pkinetisk_spi->PUSHR = c | (pcs_command << 16) | SPI_PUSHR_CTAS(0) | SPI_PUSHR_CONT;
		while (((_pkinetisk_spi->SR) & (15 << 12)) > (0 << 12)) ; // wait if FIFO full

		// See if there are any return values to pop 
	    while (((_pkinetisk_spi->SR) >> 4) & 0xf)  {
	        r = _pkinetisk_spi->POPR;
	    }

	    // readdata
		_pkinetisk_spi->PUSHR = 0 | (pcs_data << 16) | SPI_PUSHR_CTAS(0);
		while (((_pkinetisk_spi->SR) & (15 << 12)) > (0 << 12)) ; // wait if FIFO full

		// See if there are any return values to pop 
	    while (((_pkinetisk_spi->SR) >> 4) & 0xf)  {
	        r = _pkinetisk_spi->POPR;
	    }
	    // Now wait until completed.
	    wTimeout = 0xffff;
	    while (((_pkinetisk_spi->SR) & (15 << 12)) && (--wTimeout)) ; // wait until empty

	    // Make sure the last frame has been sent...
	    _pkinetisk_spi->SR = SPI_SR_TCF;   // dlear it out;
	    wTimeout = 0xffff;
	    while (!((_pkinetisk_spi->SR) & SPI_SR_TCF) && (--wTimeout)) ; // wait until it says the last frame completed

	    wTimeout = 0x10;    // should not go more than 4...
	    // lets get all of the values on the FIFO
	    while ((((_pkinetisk_spi->SR) >> 4) & 0xf) && (--wTimeout))  {
	        r = _pkinetisk_spi->POPR;
	    }

	}  
    endSPITransaction();
    return r;  // get the received byte... should check for it first...
}

// Read Pixel at x,y and get back 16-bit packed color
uint16_t ILI9341_t3n::readPixel(int16_t x, int16_t y)
{
	// Now if we are in buffer mode can return real fast
	if (_use_fbtft) {
		return _pfbtft[y*_width + x] ;
	}

	uint8_t dummy __attribute__((unused));
	uint8_t r,g,b;

	_pspin->beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

	setAddr(x, y, x, y);
	writecommand_cont(ILI9341_RAMRD); // read from RAM
	_pspin->waitTransmitComplete();

	// Push 4 bytes over SPI
	_pkinetisk_spi->PUSHR = 0 | (pcs_data << 16) | SPI_PUSHR_CTAS(0)| SPI_PUSHR_CONT;
	_pspin->waitFifoEmpty();    // wait for both queues to be empty.

	_pkinetisk_spi->PUSHR = 0 | (pcs_data << 16) | SPI_PUSHR_CTAS(0)| SPI_PUSHR_CONT;
	_pkinetisk_spi->PUSHR = 0 | (pcs_data << 16) | SPI_PUSHR_CTAS(0)| SPI_PUSHR_CONT;
	_pkinetisk_spi->PUSHR = 0 | (pcs_data << 16) | SPI_PUSHR_CTAS(0)| SPI_PUSHR_EOQ;

	// Wait for End of Queue
	while ((_pkinetisk_spi->SR & SPI_SR_EOQF) == 0) ;
	_pkinetisk_spi->SR = SPI_SR_EOQF;  // make sure it is clear

	// Read Pixel Data
	dummy = _pkinetisk_spi->POPR;	// Read a DUMMY byte of GRAM
	r = _pkinetisk_spi->POPR;		// Read a RED byte of GRAM
	g = _pkinetisk_spi->POPR;		// Read a GREEN byte of GRAM
	b = _pkinetisk_spi->POPR;		// Read a BLUE byte of GRAM

	endSPITransaction();
	return color565(r,g,b);
}

// Now lets see if we can read in multiple pixels
void ILI9341_t3n::readRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *pcolors)
{
	if (_use_fbtft) {
		uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
		for (;h>0; h--) {
			uint16_t * pfbPixel = pfbPixel_row;
			for (int i = 0 ;i < w; i++) {
				*pcolors++ = *pfbPixel++;
			}
			pfbPixel_row += _width;
		}
		return;	
	}
	

	uint8_t dummy __attribute__((unused));
//	uint8_t r,g,b;
	uint8_t rgb[3];		// read into an array...
	uint8_t rgb_index = 0;
	uint16_t c = w * h;

	_pspin->beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

	setAddr(x, y, x+w-1, y+h-1);
	writecommand_cont(ILI9341_RAMRD); // read from RAM
	_pspin->waitTransmitComplete();
	_pkinetisk_spi->PUSHR = 0 | (pcs_data << 16) | SPI_PUSHR_CTAS(0)| SPI_PUSHR_CONT | SPI_PUSHR_EOQ;
	while ((_pkinetisk_spi->SR & SPI_SR_EOQF) == 0) ;
	_pkinetisk_spi->SR = SPI_SR_EOQF;  // make sure it is clear
	while ((_pkinetisk_spi->SR & 0xf0)) {
		dummy = _pkinetisk_spi->POPR;	// Read a DUMMY byte but only once
	}
	c *= 3; // number of bytes we will transmit to the display
	while (c--) {
        	if (c) {
            		_pkinetisk_spi->PUSHR = 0 | (pcs_data << 16) | SPI_PUSHR_CTAS(0)| SPI_PUSHR_CONT;
        	} else {
            		_pkinetisk_spi->PUSHR = 0 | (pcs_data << 16) | SPI_PUSHR_CTAS(0)| SPI_PUSHR_EOQ;
		}

		// If last byte wait until all have come in...
		if (c == 0) {
			while ((_pkinetisk_spi->SR & SPI_SR_EOQF) == 0) ;
			_pkinetisk_spi->SR = SPI_SR_EOQF;  // make sure it is clear
		}
#if 0
		if ((_pkinetisk_spi->SR & 0xf0) >= 0x30) { // do we have at least 3 bytes in queue if so extract...
			r = _pkinetisk_spi->POPR;		// Read a RED byte of GRAM
			g = _pkinetisk_spi->POPR;		// Read a GREEN byte of GRAM
			b = _pkinetisk_spi->POPR;		// Read a BLUE byte of GRAM
			*pcolors++ = color565(r,g,b);
		}
#else 
		while ((_pkinetisk_spi->SR & 0xf0) > 0x00) { // do we have at least 3 bytes in queue if so extract...
			rgb[rgb_index++] = _pkinetisk_spi->POPR;		// Read in the next byte of the color
			if (rgb_index == 3) {
				*pcolors++ = color565(rgb[0],rgb[1],rgb[2]);
				rgb_index = 0;		// set index back to 0...
			}
		}
#endif
		// like waitFiroNotFull but does not pop our return queue
		if (_pspin->sizeFIFO() >= 4)
			while ((_pkinetisk_spi->SR & (15 << 12)) > (3 << 12)) ;
		else
			while ((_pkinetisk_spi->SR & (15 << 12)) > (0 << 12)) ;

	}

	endSPITransaction();
}

// Now lets see if we can writemultiple pixels
void ILI9341_t3n::writeRect(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pcolors)
{
	if (_use_fbtft) {
		uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
		for (;h>0; h--) {
			uint16_t * pfbPixel = pfbPixel_row;
			for (int i = 0 ;i < w; i++) {
				*pfbPixel++ = *pcolors++;
			}
			pfbPixel_row += _width;
		}
		return;	
	}

   	beginSPITransaction();
	setAddr(x, y, x+w-1, y+h-1);
	writecommand_cont(ILI9341_RAMWR);
	for(y=h; y>0; y--) {
		for(x=w; x>1; x--) {
			writedata16_cont(*pcolors++);
		}
		writedata16_last(*pcolors++);
	}
	endSPITransaction();
}

// writeRect8BPP - 	write 8 bit per pixel paletted bitmap
//					bitmap data in array at pixels, one byte per pixel
//					color palette data in array at palette
void ILI9341_t3n::writeRect8BPP(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *pixels, const uint16_t * palette )
{
	if (_use_fbtft) {
		uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
		for (;h>0; h--) {
			uint16_t * pfbPixel = pfbPixel_row;
			for (int i = 0 ;i < w; i++) {
				*pfbPixel++ = palette[*pixels++];
			}
			pfbPixel_row += _width;
		}
		return;	
	}

	beginSPITransaction();
	setAddr(x, y, x+w-1, y+h-1);
	writecommand_cont(ILI9341_RAMWR);
	for(y=h; y>0; y--) {
		for(x=w; x>1; x--) {
			writedata16_cont(palette[*pixels++]);
		}
		writedata16_last(palette[*pixels++]);
	}
	endSPITransaction();
}

// writeRect4BPP - 	write 4 bit per pixel paletted bitmap
//					bitmap data in array at pixels, 4 bits per pixel
//					color palette data in array at palette
//					width must be at least 2 pixels
void ILI9341_t3n::writeRect4BPP(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *pixels, const uint16_t * palette )
{
	if (_use_fbtft) {
		uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
		for (;h>0; h--) {
			uint16_t * pfbPixel = pfbPixel_row;
			for (int i = 0 ;i < w; i+=2) {
				*pfbPixel++ = palette[((*pixels)>>4)&0xF];
				*pfbPixel++ = palette[(*pixels++)&0xF];
			}
			pfbPixel_row += _width;
		}
		return;	
	}

	beginSPITransaction();
	setAddr(x, y, x+w-1, y+h-1);
	writecommand_cont(ILI9341_RAMWR);
	for(y=h; y>0; y--) {
		for(x=w; x>2; x-=2) {
			writedata16_cont(palette[((*pixels)>>4)&0xF]);
			writedata16_cont(palette[(*pixels++)&0xF]);
		}
		writedata16_cont(palette[((*pixels)>>4)&0xF]);
		writedata16_last(palette[(*pixels++)&0xF]);
	}
	endSPITransaction();
}

// writeRect2BPP - 	write 2 bit per pixel paletted bitmap
//					bitmap data in array at pixels, 4 bits per pixel
//					color palette data in array at palette
//					width must be at least 4 pixels
void ILI9341_t3n::writeRect2BPP(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *pixels, const uint16_t * palette )
{
	if (_use_fbtft) {
		uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
		for (;h>0; h--) {
			uint16_t * pfbPixel = pfbPixel_row;
			for (int i = 0 ;i < w; i+=4) {
				*pfbPixel++ = palette[((*pixels)>>6)&0x3];
				*pfbPixel++ = palette[((*pixels)>>4)&0x3];
				*pfbPixel++ = palette[((*pixels)>>2)&0x3];
				*pfbPixel++ = palette[(*pixels++)&0x3];
			}
			pfbPixel_row += _width;
		}
		return;	
	}
	beginSPITransaction();
	setAddr(x, y, x+w-1, y+h-1);
	writecommand_cont(ILI9341_RAMWR);
	for(y=h; y>0; y--) {
		for(x=w; x>4; x-=4) {
			//unrolled loop might be faster?
			writedata16_cont(palette[((*pixels)>>6)&0x3]);
			writedata16_cont(palette[((*pixels)>>4)&0x3]);
			writedata16_cont(palette[((*pixels)>>2)&0x3]);
			writedata16_cont(palette[(*pixels++)&0x3]);
		}
		writedata16_cont(palette[((*pixels)>>6)&0x3]);
		writedata16_cont(palette[((*pixels)>>4)&0x3]);
		writedata16_cont(palette[((*pixels)>>2)&0x3]);
		writedata16_last(palette[(*pixels++)&0x3]);
	}
	endSPITransaction();
}

// writeRect1BPP - 	write 1 bit per pixel paletted bitmap
//					bitmap data in array at pixels, 4 bits per pixel
//					color palette data in array at palette
//					width must be at least 8 pixels
void ILI9341_t3n::writeRect1BPP(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *pixels, const uint16_t * palette )
{
	if (_use_fbtft) {
		uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
		for (;h>0; h--) {
			uint16_t * pfbPixel = pfbPixel_row;
			for (int i = 0 ;i < w; i+=8) {
				*pfbPixel++ = palette[((*pixels)>>7)&0x1];
				*pfbPixel++ = palette[((*pixels)>>6)&0x1];
				*pfbPixel++ = palette[((*pixels)>>5)&0x1];
				*pfbPixel++ = palette[((*pixels)>>4)&0x1];
				*pfbPixel++ = palette[((*pixels)>>3)&0x1];
				*pfbPixel++ = palette[((*pixels)>>2)&0x1];
				*pfbPixel++ = palette[((*pixels)>>1)&0x1];
				*pfbPixel++ = palette[(*pixels++)&0x1];
			}
			pfbPixel_row += _width;
		}
		return;	
	}
	beginSPITransaction();
	setAddr(x, y, x+w-1, y+h-1);
	writecommand_cont(ILI9341_RAMWR);
	for(y=h; y>0; y--) {
		for(x=w; x>8; x-=8) {
			//unrolled loop might be faster?
			writedata16_cont(palette[((*pixels)>>7)&0x1]);
			writedata16_cont(palette[((*pixels)>>6)&0x1]);
			writedata16_cont(palette[((*pixels)>>5)&0x1]);
			writedata16_cont(palette[((*pixels)>>4)&0x1]);
			writedata16_cont(palette[((*pixels)>>3)&0x1]);
			writedata16_cont(palette[((*pixels)>>2)&0x1]);
			writedata16_cont(palette[((*pixels)>>1)&0x1]);
			writedata16_cont(palette[(*pixels++)&0x1]);
		}
		writedata16_cont(palette[((*pixels)>>7)&0x1]);
		writedata16_cont(palette[((*pixels)>>6)&0x1]);
		writedata16_cont(palette[((*pixels)>>5)&0x1]);
		writedata16_cont(palette[((*pixels)>>4)&0x1]);
		writedata16_cont(palette[((*pixels)>>3)&0x1]);
		writedata16_cont(palette[((*pixels)>>2)&0x1]);
		writedata16_cont(palette[((*pixels)>>1)&0x1]);
		writedata16_last(palette[(*pixels++)&0x1]);
	}
	endSPITransaction();
}


static const uint8_t init_commands[] = {
	4, 0xEF, 0x03, 0x80, 0x02,
	4, 0xCF, 0x00, 0XC1, 0X30,
	5, 0xED, 0x64, 0x03, 0X12, 0X81,
	4, 0xE8, 0x85, 0x00, 0x78,
	6, 0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02,
	2, 0xF7, 0x20,
	3, 0xEA, 0x00, 0x00,
	2, ILI9341_PWCTR1, 0x23, // Power control
	2, ILI9341_PWCTR2, 0x10, // Power control
	3, ILI9341_VMCTR1, 0x3e, 0x28, // VCM control
	2, ILI9341_VMCTR2, 0x86, // VCM control2
	2, ILI9341_MADCTL, 0x48, // Memory Access Control
	2, ILI9341_PIXFMT, 0x55,
	3, ILI9341_FRMCTR1, 0x00, 0x18,
	4, ILI9341_DFUNCTR, 0x08, 0x82, 0x27, // Display Function Control
	2, 0xF2, 0x00, // Gamma Function Disable
	2, ILI9341_GAMMASET, 0x01, // Gamma curve selected
	16, ILI9341_GMCTRP1, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08,
		0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00, // Set Gamma
	16, ILI9341_GMCTRN1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07,
		0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F, // Set Gamma
	0
};

void ILI9341_t3n::begin(void)
{
    // verify SPI pins are valid;
	// allow user to say use current ones...
	if ((_mosi != 255) || (_miso != 255) || (_sclk != 255))
	{
		// User specified pins make sure they are valid. 
		if (_pspin->pinIsMOSI(_mosi) && (_pspin->pinIsMISO(_miso)) && (_pspin->pinIsSCK(_sclk)))
		{
			Serial.printf("MOSI:%d MISO:%d SCK:%d\n\r", _mosi, _miso, _sclk);			
	        _pspin->setMOSI(_mosi);
	        _pspin->setMISO(_miso);
	        _pspin->setSCK(_sclk);
		} else {
        	return; // not valid pins...
		}
	}
	_pspin->begin();
	if (_pspin->pinIsChipSelect(_cs, _dc)) {
		pcs_data = _pspin->setCS(_cs);
		pcs_command = pcs_data | _pspin->setCS(_dc);
	} else {
		// See if at least DC is on chipselect pin, if so try to limp along...
		if (_pspin->pinIsChipSelect(_dc)) {
			pcs_data = 0;
			pcs_command = pcs_data | _pspin->setCS(_dc);
			pinMode(_cs, OUTPUT);
			_csport    = portOutputRegister(digitalPinToPort(_cs));
  			_cspinmask = digitalPinToBitMask(_cs);
  			Serial.printf("CS(%d) not cs: %x %x\n\r", _cs, _csport, _cspinmask);
		} else {
			pcs_data = 0;
			pcs_command = 0;
			return;
		}
	}

	// toggle RST low to reset
	if (_rst < 255) {
		pinMode(_rst, OUTPUT);
		digitalWrite(_rst, HIGH);
		delay(5);
		digitalWrite(_rst, LOW);
		delay(20);
		digitalWrite(_rst, HIGH);
		delay(150);
	}
/*
	uint8_t x = readcommand8(ILI9341_RDMODE);
	Serial.print("\nDisplay Power Mode: 0x"); Serial.println(x, HEX);
	x = readcommand8(ILI9341_RDMADCTL);
	Serial.print("\nMADCTL Mode: 0x"); Serial.println(x, HEX);
	x = readcommand8(ILI9341_RDPIXFMT);
	Serial.print("\nPixel Format: 0x"); Serial.println(x, HEX);
	x = readcommand8(ILI9341_RDIMGFMT);
	Serial.print("\nImage Format: 0x"); Serial.println(x, HEX);
	x = readcommand8(ILI9341_RDSELFDIAG);
	Serial.print("\nSelf Diagnostic: 0x"); Serial.println(x, HEX);
*/	
	beginSPITransaction();
	const uint8_t *addr = init_commands;
	while (1) {
		uint8_t count = *addr++;
		if (count-- == 0) break;
		writecommand_cont(*addr++);
		while (count-- > 0) {
			writedata8_cont(*addr++);
		}
	}
	writecommand_last(ILI9341_SLPOUT);    // Exit Sleep
	endSPITransaction();
	delay(120); 		
	beginSPITransaction();
	writecommand_last(ILI9341_DISPON);    // Display on
	endSPITransaction();
}




/*
This is the core graphics library for all our displays, providing a common
set of graphics primitives (points, lines, circles, etc.).  It needs to be
paired with a hardware-specific library for each display device we carry
(to handle the lower-level functions).

Adafruit invests time and resources providing this open source code, please
support Adafruit & open-source hardware by purchasing products from Adafruit!

Copyright (c) 2013 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

//#include "glcdfont.c"
extern "C" const unsigned char glcdfont[];

// Draw a circle outline
void ILI9341_t3n::drawCircle(int16_t x0, int16_t y0, int16_t r,
    uint16_t color) {
  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;

  drawPixel(x0  , y0+r, color);
  drawPixel(x0  , y0-r, color);
  drawPixel(x0+r, y0  , color);
  drawPixel(x0-r, y0  , color);

  while (x<y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;

    drawPixel(x0 + x, y0 + y, color);
    drawPixel(x0 - x, y0 + y, color);
    drawPixel(x0 + x, y0 - y, color);
    drawPixel(x0 - x, y0 - y, color);
    drawPixel(x0 + y, y0 + x, color);
    drawPixel(x0 - y, y0 + x, color);
    drawPixel(x0 + y, y0 - x, color);
    drawPixel(x0 - y, y0 - x, color);
  }
}

void ILI9341_t3n::drawCircleHelper( int16_t x0, int16_t y0,
               int16_t r, uint8_t cornername, uint16_t color) {
  int16_t f     = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x     = 0;
  int16_t y     = r;

  while (x<y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f     += ddF_y;
    }
    x++;
    ddF_x += 2;
    f     += ddF_x;
    if (cornername & 0x4) {
      drawPixel(x0 + x, y0 + y, color);
      drawPixel(x0 + y, y0 + x, color);
    }
    if (cornername & 0x2) {
      drawPixel(x0 + x, y0 - y, color);
      drawPixel(x0 + y, y0 - x, color);
    }
    if (cornername & 0x8) {
      drawPixel(x0 - y, y0 + x, color);
      drawPixel(x0 - x, y0 + y, color);
    }
    if (cornername & 0x1) {
      drawPixel(x0 - y, y0 - x, color);
      drawPixel(x0 - x, y0 - y, color);
    }
  }
}

void ILI9341_t3n::fillCircle(int16_t x0, int16_t y0, int16_t r,
			      uint16_t color) {
  drawFastVLine(x0, y0-r, 2*r+1, color);
  fillCircleHelper(x0, y0, r, 3, 0, color);
}

// Used to do circles and roundrects
void ILI9341_t3n::fillCircleHelper(int16_t x0, int16_t y0, int16_t r,
    uint8_t cornername, int16_t delta, uint16_t color) {

  int16_t f     = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x     = 0;
  int16_t y     = r;

  while (x<y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f     += ddF_y;
    }
    x++;
    ddF_x += 2;
    f     += ddF_x;

    if (cornername & 0x1) {
      drawFastVLine(x0+x, y0-y, 2*y+1+delta, color);
      drawFastVLine(x0+y, y0-x, 2*x+1+delta, color);
    }
    if (cornername & 0x2) {
      drawFastVLine(x0-x, y0-y, 2*y+1+delta, color);
      drawFastVLine(x0-y, y0-x, 2*x+1+delta, color);
    }
  }
}


// Bresenham's algorithm - thx wikpedia
void ILI9341_t3n::drawLine(int16_t x0, int16_t y0,
	int16_t x1, int16_t y1, uint16_t color)
{
	if (y0 == y1) {
		if (x1 > x0) {
			drawFastHLine(x0, y0, x1 - x0 + 1, color);
		} else if (x1 < x0) {
			drawFastHLine(x1, y0, x0 - x1 + 1, color);
		} else {
			drawPixel(x0, y0, color);
		}
		return;
	} else if (x0 == x1) {
		if (y1 > y0) {
			drawFastVLine(x0, y0, y1 - y0 + 1, color);
		} else {
			drawFastVLine(x0, y1, y0 - y1 + 1, color);
		}
		return;
	}

	bool steep = abs(y1 - y0) > abs(x1 - x0);
	if (steep) {
		swap(x0, y0);
		swap(x1, y1);
	}
	if (x0 > x1) {
		swap(x0, x1);
		swap(y0, y1);
	}

	int16_t dx, dy;
	dx = x1 - x0;
	dy = abs(y1 - y0);

	int16_t err = dx / 2;
	int16_t ystep;

	if (y0 < y1) {
		ystep = 1;
	} else {
		ystep = -1;
	}

	beginSPITransaction();
	int16_t xbegin = x0;
	if (steep) {
		for (; x0<=x1; x0++) {
			err -= dy;
			if (err < 0) {
				int16_t len = x0 - xbegin;
				if (len) {
					VLine(y0, xbegin, len + 1, color);
				} else {
					Pixel(y0, x0, color);
				}
				xbegin = x0 + 1;
				y0 += ystep;
				err += dx;
			}
		}
		if (x0 > xbegin + 1) {
			VLine(y0, xbegin, x0 - xbegin, color);
		}

	} else {
		for (; x0<=x1; x0++) {
			err -= dy;
			if (err < 0) {
				int16_t len = x0 - xbegin;
				if (len) {
					HLine(xbegin, y0, len + 1, color);
				} else {
					Pixel(x0, y0, color);
				}
				xbegin = x0 + 1;
				y0 += ystep;
				err += dx;
			}
		}
		if (x0 > xbegin + 1) {
			HLine(xbegin, y0, x0 - xbegin, color);
		}
	}
	writecommand_last(ILI9341_NOP);
	endSPITransaction();
}

// Draw a rectangle
void ILI9341_t3n::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
	if (_use_fbtft) {
		drawFastHLine(x, y, w, color);
		drawFastHLine(x, y+h-1, w, color);
		drawFastVLine(x, y, h, color);
		drawFastVLine(x+w-1, y, h, color);
	} else {
	    beginSPITransaction();
		HLine(x, y, w, color);
		HLine(x, y+h-1, w, color);
		VLine(x, y, h, color);
		VLine(x+w-1, y, h, color);
		writecommand_last(ILI9341_NOP);
		endSPITransaction();
	}
}

// Draw a rounded rectangle
void ILI9341_t3n::drawRoundRect(int16_t x, int16_t y, int16_t w,
  int16_t h, int16_t r, uint16_t color) {
  // smarter version
  drawFastHLine(x+r  , y    , w-2*r, color); // Top
  drawFastHLine(x+r  , y+h-1, w-2*r, color); // Bottom
  drawFastVLine(x    , y+r  , h-2*r, color); // Left
  drawFastVLine(x+w-1, y+r  , h-2*r, color); // Right
  // draw four corners
  drawCircleHelper(x+r    , y+r    , r, 1, color);
  drawCircleHelper(x+w-r-1, y+r    , r, 2, color);
  drawCircleHelper(x+w-r-1, y+h-r-1, r, 4, color);
  drawCircleHelper(x+r    , y+h-r-1, r, 8, color);
}

// Fill a rounded rectangle
void ILI9341_t3n::fillRoundRect(int16_t x, int16_t y, int16_t w,
				 int16_t h, int16_t r, uint16_t color) {
  // smarter version
  fillRect(x+r, y, w-2*r, h, color);

  // draw four corners
  fillCircleHelper(x+w-r-1, y+r, r, 1, h-2*r-1, color);
  fillCircleHelper(x+r    , y+r, r, 2, h-2*r-1, color);
}

// Draw a triangle
void ILI9341_t3n::drawTriangle(int16_t x0, int16_t y0,
				int16_t x1, int16_t y1,
				int16_t x2, int16_t y2, uint16_t color) {
  drawLine(x0, y0, x1, y1, color);
  drawLine(x1, y1, x2, y2, color);
  drawLine(x2, y2, x0, y0, color);
}

// Fill a triangle
void ILI9341_t3n::fillTriangle ( int16_t x0, int16_t y0,
				  int16_t x1, int16_t y1,
				  int16_t x2, int16_t y2, uint16_t color) {

  int16_t a, b, y, last;

  // Sort coordinates by Y order (y2 >= y1 >= y0)
  if (y0 > y1) {
    swap(y0, y1); swap(x0, x1);
  }
  if (y1 > y2) {
    swap(y2, y1); swap(x2, x1);
  }
  if (y0 > y1) {
    swap(y0, y1); swap(x0, x1);
  }

  if(y0 == y2) { // Handle awkward all-on-same-line case as its own thing
    a = b = x0;
    if(x1 < a)      a = x1;
    else if(x1 > b) b = x1;
    if(x2 < a)      a = x2;
    else if(x2 > b) b = x2;
    drawFastHLine(a, y0, b-a+1, color);
    return;
  }

  int16_t
    dx01 = x1 - x0,
    dy01 = y1 - y0,
    dx02 = x2 - x0,
    dy02 = y2 - y0,
    dx12 = x2 - x1,
    dy12 = y2 - y1,
    sa   = 0,
    sb   = 0;

  // For upper part of triangle, find scanline crossings for segments
  // 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
  // is included here (and second loop will be skipped, avoiding a /0
  // error there), otherwise scanline y1 is skipped here and handled
  // in the second loop...which also avoids a /0 error here if y0=y1
  // (flat-topped triangle).
  if(y1 == y2) last = y1;   // Include y1 scanline
  else         last = y1-1; // Skip it

  for(y=y0; y<=last; y++) {
    a   = x0 + sa / dy01;
    b   = x0 + sb / dy02;
    sa += dx01;
    sb += dx02;
    /* longhand:
    a = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
    b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    */
    if(a > b) swap(a,b);
    drawFastHLine(a, y, b-a+1, color);
  }

  // For lower part of triangle, find scanline crossings for segments
  // 0-2 and 1-2.  This loop is skipped if y1=y2.
  sa = dx12 * (y - y1);
  sb = dx02 * (y - y0);
  for(; y<=y2; y++) {
    a   = x1 + sa / dy12;
    b   = x0 + sb / dy02;
    sa += dx12;
    sb += dx02;
    /* longhand:
    a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
    b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    */
    if(a > b) swap(a,b);
    drawFastHLine(a, y, b-a+1, color);
  }
}

void ILI9341_t3n::drawBitmap(int16_t x, int16_t y,
			      const uint8_t *bitmap, int16_t w, int16_t h,
			      uint16_t color) {

  int16_t i, j, byteWidth = (w + 7) / 8;

  for(j=0; j<h; j++) {
    for(i=0; i<w; i++ ) {
      if(pgm_read_byte(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7))) {
	drawPixel(x+i, y+j, color);
      }
    }
  }
}

size_t ILI9341_t3n::write(uint8_t c)
{
	if (font) {
		if (c == '\n') {
			cursor_y += font->line_space; // Fix linefeed. Added by T.T., SoftEgg
			cursor_x = 0;
		} else {
			drawFontChar(c);
		}
	} else {
		if (c == '\n') {
			cursor_y += textsize*8;
			cursor_x  = 0;
		} else if (c == '\r') {
			// skip em
		} else {
			drawChar(cursor_x, cursor_y, c, textcolor, textbgcolor, textsize);
			cursor_x += textsize*6;
			if (wrap && (cursor_x > (_width - textsize*6))) {
				cursor_y += textsize*8;
				cursor_x = 0;
			}
		}
	}
	return 1;
}

// Draw a character
void ILI9341_t3n::drawChar(int16_t x, int16_t y, unsigned char c,
			    uint16_t fgcolor, uint16_t bgcolor, uint8_t size)
{
	if((x >= _width)            || // Clip right
	   (y >= _height)           || // Clip bottom
	   ((x + 6 * size - 1) < 0) || // Clip left  TODO: is this correct?
	   ((y + 8 * size - 1) < 0))   // Clip top   TODO: is this correct?
		return;

	if (fgcolor == bgcolor) {
		// This transparent approach is only about 20% faster
		if (size == 1) {
			uint8_t mask = 0x01;
			int16_t xoff, yoff;
			for (yoff=0; yoff < 8; yoff++) {
				uint8_t line = 0;
				for (xoff=0; xoff < 5; xoff++) {
					if (glcdfont[c * 5 + xoff] & mask) line |= 1;
					line <<= 1;
				}
				line >>= 1;
				xoff = 0;
				while (line) {
					if (line == 0x1F) {
						drawFastHLine(x + xoff, y + yoff, 5, fgcolor);
						break;
					} else if (line == 0x1E) {
						drawFastHLine(x + xoff, y + yoff, 4, fgcolor);
						break;
					} else if ((line & 0x1C) == 0x1C) {
						drawFastHLine(x + xoff, y + yoff, 3, fgcolor);
						line <<= 4;
						xoff += 4;
					} else if ((line & 0x18) == 0x18) {
						drawFastHLine(x + xoff, y + yoff, 2, fgcolor);
						line <<= 3;
						xoff += 3;
					} else if ((line & 0x10) == 0x10) {
						drawPixel(x + xoff, y + yoff, fgcolor);
						line <<= 2;
						xoff += 2;
					} else {
						line <<= 1;
						xoff += 1;
					}
				}
				mask = mask << 1;
			}
		} else {
			uint8_t mask = 0x01;
			int16_t xoff, yoff;
			for (yoff=0; yoff < 8; yoff++) {
				uint8_t line = 0;
				for (xoff=0; xoff < 5; xoff++) {
					if (glcdfont[c * 5 + xoff] & mask) line |= 1;
					line <<= 1;
				}
				line >>= 1;
				xoff = 0;
				while (line) {
					if (line == 0x1F) {
						fillRect(x + xoff * size, y + yoff * size,
							5 * size, size, fgcolor);
						break;
					} else if (line == 0x1E) {
						fillRect(x + xoff * size, y + yoff * size,
							4 * size, size, fgcolor);
						break;
					} else if ((line & 0x1C) == 0x1C) {
						fillRect(x + xoff * size, y + yoff * size,
							3 * size, size, fgcolor);
						line <<= 4;
						xoff += 4;
					} else if ((line & 0x18) == 0x18) {
						fillRect(x + xoff * size, y + yoff * size,
							2 * size, size, fgcolor);
						line <<= 3;
						xoff += 3;
					} else if ((line & 0x10) == 0x10) {
						fillRect(x + xoff * size, y + yoff * size,
							size, size, fgcolor);
						line <<= 2;
						xoff += 2;
					} else {
						line <<= 1;
						xoff += 1;
					}
				}
				mask = mask << 1;
			}
		}
	} else {
		// This solid background approach is about 5 time faster
		beginSPITransaction();
		setAddr(x, y, x + 6 * size - 1, y + 8 * size - 1);
		writecommand_cont(ILI9341_RAMWR);
		uint8_t xr, yr;
		uint8_t mask = 0x01;
		uint16_t color;
		for (y=0; y < 8; y++) {
			for (yr=0; yr < size; yr++) {
				for (x=0; x < 5; x++) {
					if (glcdfont[c * 5 + x] & mask) {
						color = fgcolor;
					} else {
						color = bgcolor;
					}
					for (xr=0; xr < size; xr++) {
						writedata16_cont(color);
					}
				}
				for (xr=0; xr < size; xr++) {
					writedata16_cont(bgcolor);
				}
			}
			mask = mask << 1;
		}
		writecommand_last(ILI9341_NOP);
		endSPITransaction();
	}
}

static uint32_t fetchbit(const uint8_t *p, uint32_t index)
{
	if (p[index >> 3] & (1 << (7 - (index & 7)))) return 1;
	return 0;
}

static uint32_t fetchbits_unsigned(const uint8_t *p, uint32_t index, uint32_t required)
{
	uint32_t val = 0;
	do {
		uint8_t b = p[index >> 3];
		uint32_t avail = 8 - (index & 7);
		if (avail <= required) {
			val <<= avail;
			val |= b & ((1 << avail) - 1);
			index += avail;
			required -= avail;
		} else {
			b >>= avail - required;
			val <<= required;
			val |= b & ((1 << required) - 1);
			break;
		}
	} while (required);
	return val;
}

static uint32_t fetchbits_signed(const uint8_t *p, uint32_t index, uint32_t required)
{
	uint32_t val = fetchbits_unsigned(p, index, required);
	if (val & (1 << (required - 1))) {
		return (int32_t)val - (1 << required);
	}
	return (int32_t)val;
}


void ILI9341_t3n::drawFontChar(unsigned int c)
{
	uint32_t bitoffset;
	const uint8_t *data;

	//Serial.printf("drawFontChar %d\n", c);

	if (c >= font->index1_first && c <= font->index1_last) {
		bitoffset = c - font->index1_first;
		bitoffset *= font->bits_index;
	} else if (c >= font->index2_first && c <= font->index2_last) {
		bitoffset = c - font->index2_first + font->index1_last - font->index1_first + 1;
		bitoffset *= font->bits_index;
	} else if (font->unicode) {
		return; // TODO: implement sparse unicode
	} else {
		return;
	}
	//Serial.printf("  index =  %d\n", fetchbits_unsigned(font->index, bitoffset, font->bits_index));
	data = font->data + fetchbits_unsigned(font->index, bitoffset, font->bits_index);

	uint32_t encoding = fetchbits_unsigned(data, 0, 3);
	if (encoding != 0) return;
	uint32_t width = fetchbits_unsigned(data, 3, font->bits_width);
	bitoffset = font->bits_width + 3;
	uint32_t height = fetchbits_unsigned(data, bitoffset, font->bits_height);
	bitoffset += font->bits_height;
	//Serial.printf("  size =   %d,%d\n", width, height);

	int32_t xoffset = fetchbits_signed(data, bitoffset, font->bits_xoffset);
	bitoffset += font->bits_xoffset;
	int32_t yoffset = fetchbits_signed(data, bitoffset, font->bits_yoffset);
	bitoffset += font->bits_yoffset;
	//Serial.printf("  offset = %d,%d\n", xoffset, yoffset);

	uint32_t delta = fetchbits_unsigned(data, bitoffset, font->bits_delta);
	bitoffset += font->bits_delta;
	//Serial.printf("  delta =  %d\n", delta);

	//Serial.printf("  cursor = %d,%d\n", cursor_x, cursor_y);

	// horizontally, we draw every pixel, or none at all
	if (cursor_x < 0) cursor_x = 0;
	int32_t origin_x = cursor_x + xoffset;
	if (origin_x < 0) {
		cursor_x -= xoffset;
		origin_x = 0;
	}
	if (origin_x + (int)width > _width) {
		if (!wrap) return;
		origin_x = 0;
		if (xoffset >= 0) {
			cursor_x = 0;
		} else {
			cursor_x = -xoffset;
		}
		cursor_y += font->line_space;
	}
	if (cursor_y >= _height) return;
	cursor_x += delta;

	// vertically, the top and/or bottom can be clipped
	int32_t origin_y = cursor_y + font->cap_height - height - yoffset;
	//Serial.printf("  origin = %d,%d\n", origin_x, origin_y);

	// TODO: compute top skip and number of lines
	int32_t linecount = height;
	//uint32_t loopcount = 0;
	uint32_t y = origin_y;
	while (linecount) {
		//Serial.printf("    linecount = %d\n", linecount);
		uint32_t b = fetchbit(data, bitoffset++);
		if (b == 0) {
			//Serial.println("    single line");
			uint32_t x = 0;
			do {
				uint32_t xsize = width - x;
				if (xsize > 32) xsize = 32;
				uint32_t bits = fetchbits_unsigned(data, bitoffset, xsize);
				drawFontBits(bits, xsize, origin_x + x, y, 1);
				bitoffset += xsize;
				x += xsize;
			} while (x < width);
			y++;
			linecount--;
		} else {
			uint32_t n = fetchbits_unsigned(data, bitoffset, 3) + 2;
			bitoffset += 3;
			uint32_t x = 0;
			do {
				uint32_t xsize = width - x;
				if (xsize > 32) xsize = 32;
				//Serial.printf("    multi line %d\n", n);
				uint32_t bits = fetchbits_unsigned(data, bitoffset, xsize);
				drawFontBits(bits, xsize, origin_x + x, y, n);
				bitoffset += xsize;
				x += xsize;
			} while (x < width);
			y += n;
			linecount -= n;
		}
		//if (++loopcount > 100) {
			//Serial.println("     abort draw loop");
			//break;
		//}
	}
}
//strPixelLen			- gets pixel length of given ASCII string
int16_t ILI9341_t3n::strPixelLen(char * str)
{
//	Serial.printf("strPixelLen %s\n", str);
	if (!str) return(0);
	uint16_t len=0, maxlen=0;
	while (*str)
	{
		if (*str=='\n')
		{
			if ( len > maxlen )
			{
				maxlen=len;
				len=0;
			}
		}
		else
		{
			if (!font)
			{
				len+=textsize*6;
			}
			else
			{

				uint32_t bitoffset;
				const uint8_t *data;
				uint16_t c = *str;

//				Serial.printf("char %c(%d)\n", c,c);

				if (c >= font->index1_first && c <= font->index1_last) {
					bitoffset = c - font->index1_first;
					bitoffset *= font->bits_index;
				} else if (c >= font->index2_first && c <= font->index2_last) {
					bitoffset = c - font->index2_first + font->index1_last - font->index1_first + 1;
					bitoffset *= font->bits_index;
				} else if (font->unicode) {
					continue;
				} else {
					continue;
				}
				//Serial.printf("  index =  %d\n", fetchbits_unsigned(font->index, bitoffset, font->bits_index));
				data = font->data + fetchbits_unsigned(font->index, bitoffset, font->bits_index);

				uint32_t encoding = fetchbits_unsigned(data, 0, 3);
				if (encoding != 0) continue;
//				uint32_t width = fetchbits_unsigned(data, 3, font->bits_width);
//				Serial.printf("  width =  %d\n", width);
				bitoffset = font->bits_width + 3;
				bitoffset += font->bits_height;

//				int32_t xoffset = fetchbits_signed(data, bitoffset, font->bits_xoffset);
//				Serial.printf("  xoffset =  %d\n", xoffset);
				bitoffset += font->bits_xoffset;
				bitoffset += font->bits_yoffset;

				uint32_t delta = fetchbits_unsigned(data, bitoffset, font->bits_delta);
				bitoffset += font->bits_delta;
//				Serial.printf("  delta =  %d\n", delta);

				len += delta;//+width-xoffset;
//				Serial.printf("  len =  %d\n", len);
				if ( len > maxlen )
				{
					maxlen=len;
//					Serial.printf("  maxlen =  %d\n", maxlen);
				}
			
			}
		}
		str++;
	}
//	Serial.printf("Return  maxlen =  %d\n", maxlen);
	return( maxlen );
}

void ILI9341_t3n::drawFontBits(uint32_t bits, uint32_t numbits, uint32_t x, uint32_t y, uint32_t repeat)
{
#if 0			
	// TODO: replace this *slow* code with something fast...
	//Serial.printf("      %d bits at %d,%d: %X\n", numbits, x, y, bits);
	if (bits == 0) return;
	do {
		uint32_t x1 = x;
		uint32_t n = numbits;
		do {
			n--;
			if (bits & (1 << n)) {
				drawPixel(x1, y, textcolor);
				//Serial.printf("        pixel at %d,%d\n", x1, y);
			}
			x1++;
		} while (n > 0);
		y++;
		repeat--;
	} while (repeat);
#endif
#if 1
	if (bits == 0) return;
	if (_use_fbtft) {
		uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
		do {
			uint16_t * pfbPixel = pfbPixel_row;
			uint32_t n = numbits;
			do {
				n--;
				if (bits & (1 << n)) {
					*pfbPixel = textcolor;
				}
				pfbPixel++;
			} while (n > 0);

			pfbPixel_row += _width;
			repeat--;
		} while (repeat);

	} else {
		beginSPITransaction();
		int w = 0;
		do {
			uint32_t x1 = x;
			uint32_t n = numbits;		
			
			writecommand_cont(ILI9341_PASET); // Row addr set
			writedata16_cont(y);   // YSTART
			writedata16_cont(y);   // YEND	
			
			do {
				n--;
				if (bits & (1 << n)) {
					w++;
				}
				else if (w > 0) {
					// "drawFastHLine(x1 - w, y, w, textcolor)"
					writecommand_cont(ILI9341_CASET); // Column addr set
					writedata16_cont(x1 - w);   // XSTART
					writedata16_cont(x1);   // XEND					
					writecommand_cont(ILI9341_RAMWR);
					while (w-- > 1) { // draw line
						writedata16_cont(textcolor);
					}
					writedata16_last(textcolor);
				}
							
				x1++;
			} while (n > 0);

			if (w > 0) {
					// "drawFastHLine(x1 - w, y, w, textcolor)"
					writecommand_cont(ILI9341_CASET); // Column addr set
					writedata16_cont(x1 - w);   // XSTART
					writedata16_cont(x1);   // XEND
					writecommand_cont(ILI9341_RAMWR);				
					while (w-- > 1) { //draw line
						writedata16_cont(textcolor);
					}
					writedata16_last(textcolor);
			}
			
			y++;
			repeat--;
		} while (repeat);
		endSPITransaction();
	}
#endif	
}

void ILI9341_t3n::setCursor(int16_t x, int16_t y) {
	if (x < 0) x = 0;
	else if (x >= _width) x = _width - 1;
	cursor_x = x;
	if (y < 0) y = 0;
	else if (y >= _height) y = _height - 1;
	cursor_y = y;
}
void ILI9341_t3n::getCursor(int16_t *x, int16_t *y) {
  *x = cursor_x;
  *y = cursor_y;
}

void ILI9341_t3n::setTextSize(uint8_t s) {
  textsize = (s > 0) ? s : 1;
}

uint8_t ILI9341_t3n::getTextSize() {
	return textsize;
}

void ILI9341_t3n::setTextColor(uint16_t c) {
  // For 'transparent' background, we'll set the bg
  // to the same as fg instead of using a flag
  textcolor = textbgcolor = c;
}

void ILI9341_t3n::setTextColor(uint16_t c, uint16_t b) {
  textcolor   = c;
  textbgcolor = b;
}

void ILI9341_t3n::setTextWrap(boolean w) {
  wrap = w;
}

boolean ILI9341_t3n::getTextWrap()
{
	return wrap;
}

uint8_t ILI9341_t3n::getRotation(void) {
  return rotation;
}

void ILI9341_t3n::sleep(bool enable) {
	beginSPITransaction();
	if (enable) {
		writecommand_cont(ILI9341_DISPOFF);		
		writecommand_last(ILI9341_SLPIN);	
		  endSPITransaction();
	} else {
		writecommand_cont(ILI9341_DISPON);
		writecommand_last(ILI9341_SLPOUT);
		endSPITransaction();
		delay(5);
	}
}

void Adafruit_GFX_Button::initButton(ILI9341_t3n *gfx,
	int16_t x, int16_t y, uint8_t w, uint8_t h,
	uint16_t outline, uint16_t fill, uint16_t textcolor,
	const char *label, uint8_t textsize)
{
	_x = x;
	_y = y;
	_w = w;
	_h = h;
	_outlinecolor = outline;
	_fillcolor = fill;
	_textcolor = textcolor;
	_textsize = textsize;
	_gfx = gfx;
	strncpy(_label, label, 9);
	_label[9] = 0;
}

void Adafruit_GFX_Button::drawButton(bool inverted)
{
	uint16_t fill, outline, text;

	if (! inverted) {
		fill = _fillcolor;
		outline = _outlinecolor;
		text = _textcolor;
	} else {
		fill =  _textcolor;
		outline = _outlinecolor;
		text = _fillcolor;
	}
	_gfx->fillRoundRect(_x - (_w/2), _y - (_h/2), _w, _h, min(_w,_h)/4, fill);
	_gfx->drawRoundRect(_x - (_w/2), _y - (_h/2), _w, _h, min(_w,_h)/4, outline);
	_gfx->setCursor(_x - strlen(_label)*3*_textsize, _y-4*_textsize);
	_gfx->setTextColor(text);
	_gfx->setTextSize(_textsize);
	_gfx->print(_label);
}

bool Adafruit_GFX_Button::contains(int16_t x, int16_t y)
{
	if ((x < (_x - _w/2)) || (x > (_x + _w/2))) return false;
	if ((y < (_y - _h/2)) || (y > (_y + _h/2))) return false;
	return true;
}


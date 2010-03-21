/* -----------------------------------------------------------------------------

	Copyright (c) 2006 Simon Brown                          si@sjbrown.co.uk

	Permission is hereby granted, free of charge, to any person obtaining
	a copy of this software and associated documentation files (the 
	"Software"), to	deal in the Software without restriction, including
	without limitation the rights to use, copy, modify, merge, publish,
	distribute, sublicense, and/or sell copies of the Software, and to 
	permit persons to whom the Software is furnished to do so, subject to 
	the following conditions:

	The above copyright notice and this permission notice shall be included
	in all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
	OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
	CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
	TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
	SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
	
   -------------------------------------------------------------------------- */
   
#include <squish.h>
#include "colourset.h"
#include "maths.h"
#include "rangefit.h"
#include "clusterfit.h"
#include "colourblock.h"
#include "alpha.h"
#include "singlecolourfit.h"

namespace squish {

static int FixFlags( int flags )
{
	// grab the flag bits
	int method = flags & ( kDxt1 | kDxt3 | kDxt5 );
	int fit = flags & ( kColourIterativeClusterFit | kColourClusterFit | kColourRangeFit );
	int extra = flags & ( kWeightColourByAlpha | kSourceBGRA );
	
	// set defaults
	if( method != kDxt3 && method != kDxt5 )
		method = kDxt1;
	if( fit != kColourRangeFit && fit != kColourIterativeClusterFit )
		fit = kColourClusterFit;
		
	// done
	return method | fit | extra;
}

void CompressMasked( u8 const* rgba, int mask, void* block, int flags, float* metric )
{
	// fix any bad flags
	flags = FixFlags( flags );

	// get the block locations
	void* colourBlock = block;
	void* alphaBock = block;
	if( ( flags & ( kDxt3 | kDxt5 ) ) != 0 )
		colourBlock = reinterpret_cast< u8* >( block ) + 8;

	// create the minimal point set
	ColourSet colours( rgba, mask, flags );
	
	// check the compression type and compress colour
	if( colours.GetCount() == 1 )
	{
		// always do a single colour fit
		SingleColourFit fit( &colours, flags );
		fit.Compress( colourBlock );
	}
	else if( ( flags & kColourRangeFit ) != 0 || colours.GetCount() == 0 )
	{
		// do a range fit
		RangeFit fit( &colours, flags, metric );
		fit.Compress( colourBlock );
	}
	else
	{
		// default to a cluster fit (could be iterative or not)
		ClusterFit fit( &colours, flags, metric );
		fit.Compress( colourBlock );
	}
	
	// compress alpha separately if necessary
	if( ( flags & kDxt3 ) != 0 )
		CompressAlphaDxt3( rgba, mask, alphaBock );
	else if( ( flags & kDxt5 ) != 0 )
		CompressAlphaDxt5( rgba, mask, alphaBock );
}

void Decompress( u8* rgba, void const* block, int flags )
{
	// fix any bad flags
	flags = FixFlags( flags );

	// get the block locations
	void const* colourBlock = block;
	void const* alphaBock = block;
	if( ( flags & ( kDxt3 | kDxt5 ) ) != 0 )
		colourBlock = reinterpret_cast< u8 const* >( block ) + 8;

	// decompress colour
	DecompressColour( rgba, colourBlock, ( flags & kDxt1 ) != 0 );

	// decompress alpha separately if necessary
	if( ( flags & kDxt3 ) != 0 )
		DecompressAlphaDxt3( rgba, alphaBock );
	else if( ( flags & kDxt5 ) != 0 )
		DecompressAlphaDxt5( rgba, alphaBock );
}

int GetStorageRequirements( int width, int height, int flags )
{
	// fix any bad flags
	flags = FixFlags( flags );
	
	// compute the storage requirements
	int blockcount = ( ( width + 3 )/4 ) * ( ( height + 3 )/4 );
	int blocksize = ( ( flags & kDxt1 ) != 0 ) ? 8 : 16;
	return blockcount*blocksize;	
}

void CopyRGBA( u8 const* source, u8* dest, int flags )
{
	if (flags & kSourceBGRA)
	{
		// convert from bgra to rgba
		dest[0] = source[2];
		dest[1] = source[1];
		dest[2] = source[0];
		dest[3] = source[3];
	}
	else
	{
		for( int i = 0; i < 4; ++i )
			*dest++ = *source++;
	}
}

void CompressImage( u8 const* rgba, int width, int height, void* blocks, int flags, float* metric )
{
	CompressImage(rgba, width, height, width*4, blocks, flags, metric);
}
  
void CompressImage( u8 const* rgba, int width, int height, int pitch, void* blocks, int flags, float* metric )
{
	// fix any bad flags
	flags = FixFlags( flags );

	// initialise the block output
	u8* targetBlock = reinterpret_cast< u8* >( blocks );
	int bytesPerBlock = ( ( flags & kDxt1 ) != 0 ) ? 8 : 16;

	// loop over blocks
	for( int y = 0; y < height; y += 4 )
	{
		for( int x = 0; x < width; x += 4 )
		{
			// build the 4x4 block of pixels
			u8 sourceRgba[16*4];
			u8* targetPixel = sourceRgba;
			int mask = 0;
			for( int py = 0; py < 4; ++py )
			{
				for( int px = 0; px < 4; ++px )
				{
					// get the source pixel in the image
					int sx = x + px;
					int sy = y + py;
					
					// enable if we're in the image
					if( sx < width && sy < height )
					{
						// copy the rgba value
						u8 const* sourcePixel = rgba + pitch*sy + 4*sx;
						CopyRGBA(sourcePixel, targetPixel, flags);
						// enable this pixel
						mask |= ( 1 << ( 4*py + px ) );
					}
					targetPixel += 4;
				}
			}
			
			// compress it into the output
			CompressMasked( sourceRgba, mask, targetBlock, flags, metric );
			
			// advance
			targetBlock += bytesPerBlock;
		}
	}
}

void DecompressImage( u8* rgba, int width, int height, void const* blocks, int flags )
{
	DecompressImage( rgba, width, height, width*4, blocks, flags );
}

void DecompressImage( u8* rgba, int width, int height, int pitch, void const* blocks, int flags )
{
	// fix any bad flags
	flags = FixFlags( flags );

	// initialise the block input
	u8 const* sourceBlock = reinterpret_cast< u8 const* >( blocks );
	int bytesPerBlock = ( ( flags & kDxt1 ) != 0 ) ? 8 : 16;

	// loop over blocks
	for( int y = 0; y < height; y += 4 )
	{
		for( int x = 0; x < width; x += 4 )
		{
			// decompress the block
			u8 targetRgba[4*16];
			Decompress( targetRgba, sourceBlock, flags );
			
			// write the decompressed pixels to the correct image locations
			u8 const* sourcePixel = targetRgba;
			for( int py = 0; py < 4; ++py )
			{
				for( int px = 0; px < 4; ++px )
				{
					// get the target location
					int sx = x + px;
					int sy = y + py;
					if( sx < width && sy < height )
					{
						u8* targetPixel = rgba + pitch*sy + 4*sx;
						
						// copy the rgba value
						CopyRGBA(sourcePixel, targetPixel, flags);
					}
					sourcePixel += 4;
				}
			}
			
			// advance
			sourceBlock += bytesPerBlock;
		}
	}
}

static double ErrorSq(double x, double y)
{
	return (x - y) * (x - y);
}

void ComputeMSE( u8 const *rgba, int width, int height, u8 const *dxt, int flags, double &colourMSE, double &alphaMSE )
{
  	ComputeMSE(rgba, width, height, width*4, dxt, flags, colourMSE, alphaMSE);
}
                
void ComputeMSE( u8 const *rgba, int width, int height, int pitch, u8 const *dxt, int flags, double &colourMSE, double &alphaMSE )
{
	// fix any bad flags
	flags = FixFlags( flags );
	colourMSE = alphaMSE = 0;

	// initialise the block input
	squish::u8 const* sourceBlock = dxt;
	int bytesPerBlock = ( ( flags & squish::kDxt1 ) != 0 ) ? 8 : 16;

	// loop over blocks
	for( int y = 0; y < height; y += 4 )
	{
		for( int x = 0; x < width; x += 4 )
		{
			// decompress the block
			u8 targetRgba[4*16];
			Decompress( targetRgba, sourceBlock, flags );
			
			// write the decompressed pixels to the correct image locations
			u8 const* sourcePixel = targetRgba;
			for( int py = 0; py < 4; ++py )
			{
				for( int px = 0; px < 4; ++px )
				{
					// get the target location
					int sx = x + px;
					int sy = y + py;
					if( sx < width && sy < height )
					{
						u8 const* targetPixel = rgba + pitch*sy + 4*sx;
						u8 colour[4];
						CopyRGBA(targetPixel, colour, flags);
						// compute the MSE of colour and alpha
						double cmse = 0;
						for( int i = 0; i < 3; ++i )
							cmse += ErrorSq(sourcePixel[i], colour[i]);
						if (colour[3] == 0 && sourcePixel[3] == 0) // transparent source and dest						double cmse = 0;
							cmse = 0; // transparent in both, so colour is inconsequential
						alphaMSE += ErrorSq(colour[3], sourcePixel[3]);
						colourMSE += cmse;
					}
					sourcePixel += 4;
				}
			}
			
			// advance
			sourceBlock += bytesPerBlock;
		}
	}
	colourMSE /= (width * height * 3);
	alphaMSE /= (width * height);
}

} // namespace squish

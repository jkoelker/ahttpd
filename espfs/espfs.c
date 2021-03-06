/*
This is a simple read-only implementation of a file system. It uses a block of data coming from the
mkespfsimg tool, and can use that block to do abstracted operations on the files that are in there.
It's written for use with httpd, but doesn't need to be used as such.
*/

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */


//These routines can also be tested by comping them in with the espfstest tool. This
//simplifies debugging, but needs some slightly different headers. The #ifdef takes
//care of that.

#include <stdint.h>

#if __ets__ || ESP_PLATFORM
//esp build
#include "esp8266.h"
#else
//Test build
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ICACHE_FLASH_ATTR
#endif

#include "espfsformat.h"
#include "espfs.h"

#ifdef ESPFS_HEATSHRINK
#include "heatshrink/heatshrink_config.h"
#include "heatshrink/heatshrink_decoder.h"
#endif


//ESP8266 stores flash offsets here. ESP32, for now, stores memory locations here.
static char* espFsData = NULL;


struct EspFsFile {
	EspFsHeader *header;
	char decompressor;
	int32_t posDecomp;
	char *posStart;
	char *posComp;
	void *decompData;
};


struct EspFs {
    char *name;
    char *position;

    struct EspFs *next;
};


static struct EspFs *_fs = NULL;


/*
Available locations, at least in my flash, with boundaries partially guessed. This
is using 0.9.1/0.9.2 SDK on a not-too-new module.
0x00000 (0x10000): Code/data (RAM data?)
0x10000 (0x02000): Gets erased by something?
0x12000 (0x2E000): Free (filled with zeroes) (parts used by ESPCloud and maybe SSL)
0x40000 (0x20000): Code/data (ROM data?)
0x60000 (0x1C000): Free
0x7c000 (0x04000): Param store
0x80000 - end of flash

Accessing the flash through the mem emulation at 0x40200000 is a bit hairy: All accesses
*must* be aligned 32-bit accesses. Reading a short, byte or unaligned word will result in
a memory exception, crashing the program.
*/

#ifndef ESP32
#define FLASH_BASE_ADDR 0x40200000
#endif

//ToDo: perhaps memcpy also does unaligned accesses?
#if defined(__ets__) && !defined(ESP32)
void ICACHE_FLASH_ATTR readFlashUnaligned(char *dst, char *src, int len) {
	uint8_t src_offset = ((uint32_t)src) & 3;
	uint32_t src_address = ((uint32_t)src) - src_offset;

	uint32_t tmp_buf[len/4 + 2];
	spi_flash_read((uint32)src_address, (uint32*)tmp_buf, len+src_offset);
	memcpy(dst, ((uint8_t*)tmp_buf)+src_offset, len);
}
#else
#define readFlashUnaligned memcpy
#endif

#if defined(__ets__) && !defined(ESP32)
void ICACHE_FLASH_ATTR readFlashAligned(uint32 *dst, uint32_t pos, int len) {
	spi_flash_read(pos, dst, len);
}
#else
#define readFlashAligned(a,b,c) memcpy(a, (uint32_t*)b, c)
#endif


static void freeEspFS(void) {
    struct EspFs *f;
    while ((f = _fs) != NULL) {
        _fs = _fs->next;
        free(f->name);
        free(f);
    }
}


static void scanEspFS(void) {
	if (_fs != NULL) {
        return;
	}

	char *p = espFsData;
	char namebuf[256];

	EspFsHeader h;
    struct EspFs *f;

	while(1) {
		// Grab the next file header.
		readFlashAligned((uint32_t *)&h, (uint32_t)p, sizeof(EspFsHeader));

		if (h.magic != ESPFS_MAGIC) {
			httpd_printf("Magic mismatch. EspFS image broken.\n");
            freeEspFS();
			return;
		}

		if (h.flags & FLAG_LASTFILE) {
			httpd_printf("End of image.\n");
			break;
		}

        f = calloc(1, sizeof(*f));

        if (f == NULL) {
			httpd_printf("Failed to scan, out of memory.\n");
            freeEspFS();
			return;
        }

        f->position = p;

		// Grab the name of the file.
		p += sizeof(EspFsHeader);
		readFlashAligned((uint32_t *)&namebuf, (uint32_t)p, sizeof(namebuf));

        size_t name_len = strlen(namebuf) + 1;
        f->name = calloc(name_len, sizeof(*(f->name)));
        if (f->name == NULL) {
			httpd_printf("Failed to scan, out of memory.\n");
            free(f);
            freeEspFS();
			return;
        }

        snprintf(f->name, name_len, "%s", namebuf);

		p += h.nameLen + h.fileLenComp;
		if ((int)p & 3) {
            p += 4 - ((int)p &3 ); // align to next 32bit val
        }

        if (_fs != NULL) {
            f->next = _fs;
        }

        _fs = f;
	}
}


EspFsInitResult ICACHE_FLASH_ATTR espFsInit(void *flashAddress) {
#ifndef ESP32
	if((uint32_t)flashAddress > 0x40000000) {
		flashAddress = (void*)((uint32_t)flashAddress-FLASH_BASE_ADDR);
	}

	// base address must be aligned to 4 bytes
	if (((int)flashAddress & 3) != 0) {
		return ESPFS_INIT_RESULT_BAD_ALIGN;
	}
#endif

	// check if there is valid header at address
	EspFsHeader testHeader;
	readFlashUnaligned((char*)&testHeader, (char*)flashAddress, sizeof(EspFsHeader));
	printf("Esp magic: %x (should be %x)\n", testHeader.magic, ESPFS_MAGIC);
	if (testHeader.magic != ESPFS_MAGIC) {
		return ESPFS_INIT_RESULT_NO_IMAGE;
	}

	espFsData = (char *)flashAddress;
    scanEspFS();
	return ESPFS_INIT_RESULT_OK;
}

//Copies len bytes over from dst to src, but does it using *only*
//aligned 32-bit reads. Yes, it's no too optimized but it's short and sweet and it works.

// Returns flags of opened file.
int ICACHE_FLASH_ATTR espFsFlags(EspFsFile *fh) {
	if (fh == NULL) {
		httpd_printf("File handle not ready\n");
		return -1;
	}

	int8_t flags;
	readFlashUnaligned((char*)&flags, (char*)&fh->header->flags, 1);
	return (int)flags;
}

//Open a file and return a pointer to the file desc struct.
EspFsFile ICACHE_FLASH_ATTR *espFsOpen(char *fileName) {
	if (espFsData == NULL) {
		httpd_printf("Call espFsInit first!\n");
		return NULL;
	}

	EspFsFile *r;
    struct EspFs *f = _fs;

	// Strip initial slashes
	while(fileName[0]=='/') fileName++;

    while (f != NULL) {
		if (strcmp(f->name, fileName) == 0) {
            break;
        }

        f = f->next;
    }

    if (f == NULL) {
		httpd_printf("File not found: %s\n", fileName);
        return NULL;
    }

    r = malloc(sizeof(EspFsFile));  // Alloc file desc mem

    if (r == NULL) {
		httpd_printf("Failed to alloc file handler for file: %s\n", fileName);
        return NULL;
    }

    r->header = (EspFsHeader *)f->position;
	r->decompressor = r->header->compression;
	r->posComp = f->position + r->header->nameLen + sizeof(EspFsHeader);
	r->posStart = r->posComp;
	r->posDecomp = 0;

    if (r->header->compression == COMPRESS_NONE) {
	    r->decompData=NULL;
#ifdef ESPFS_HEATSHRINK
    } else if (r->header->compression==COMPRESS_HEATSHRINK) {
        // File is compressed with Heatshrink.
	    char parm;
	    heatshrink_decoder *dec;
	    // Decoder params are stored in 1st byte.
	    readFlashUnaligned(&parm, r->posComp, 1);
		r->posComp++;
		httpd_printf("Heatshrink compressed file; decode parms = %x\n", parm);
		dec = heatshrink_decoder_alloc(16, (parm >> 4) & 0xf, parm & 0xf);
		r->decompData = dec;
#endif
	} else {
	    httpd_printf("Invalid compression: %d\n", r->header->compression);
	    return NULL;
	}

    return r;
}


//Read len bytes from the given file into buff. Returns the actual amount of bytes read.
int ICACHE_FLASH_ATTR espFsRead(EspFsFile *fh, char *buff, int len) {
	int flen;
#ifdef ESPFS_HEATSHRINK
	int fdlen;
#endif
	if (fh==NULL) return 0;
		
	readFlashUnaligned((char*)&flen, (char*)&fh->header->fileLenComp, 4);
	//Cache file length.
	//Do stuff depending on the way the file is compressed.
	if (fh->decompressor==COMPRESS_NONE) {
		int toRead;
		toRead=flen-(fh->posComp-fh->posStart);
		if (len>toRead) len=toRead;
//		httpd_printf("Reading %d bytes from %x\n", len, (unsigned int)fh->posComp);
		readFlashUnaligned(buff, fh->posComp, len);
		fh->posDecomp+=len;
		fh->posComp+=len;
//		httpd_printf("Done reading %d bytes, pos=%x\n", len, fh->posComp);
		return len;
#ifdef ESPFS_HEATSHRINK
	} else if (fh->decompressor==COMPRESS_HEATSHRINK) {
		readFlashUnaligned((char*)&fdlen, (char*)&fh->header->fileLenDecomp, 4);
		int decoded=0;
		size_t elen, rlen;
		char ebuff[16];
		heatshrink_decoder *dec=(heatshrink_decoder *)fh->decompData;
//		httpd_printf("Alloc %p\n", dec);
		if (fh->posDecomp == fdlen) {
			return 0;
		}

		// We must ensure that whole file is decompressed and written to output buffer.
		// This means even when there is no input data (elen==0) try to poll decoder until
		// posDecomp equals decompressed file length

		while(decoded<len) {
			//Feed data into the decompressor
			//ToDo: Check ret val of heatshrink fns for errors
			elen=flen-(fh->posComp - fh->posStart);
			if (elen>0) {
				readFlashUnaligned(ebuff, fh->posComp, 16);
				heatshrink_decoder_sink(dec, (uint8_t *)ebuff, (elen>16)?16:elen, &rlen);
				fh->posComp+=rlen;
			}
			//Grab decompressed data and put into buff
			heatshrink_decoder_poll(dec, (uint8_t *)buff, len-decoded, &rlen);
			fh->posDecomp+=rlen;
			buff+=rlen;
			decoded+=rlen;

//			httpd_printf("Elen %d rlen %d d %d pd %ld fdl %d\n",elen,rlen,decoded, fh->posDecomp, fdlen);

			if (elen == 0) {
				if (fh->posDecomp == fdlen) {
//					httpd_printf("Decoder finish\n");
					heatshrink_decoder_finish(dec);
				}
				return decoded;
			}
		}
		return len;
#endif
	}
	return 0;
}

//Close the file.
void ICACHE_FLASH_ATTR espFsClose(EspFsFile *fh) {
	if (fh==NULL) return;
#ifdef ESPFS_HEATSHRINK
	if (fh->decompressor==COMPRESS_HEATSHRINK) {
		heatshrink_decoder *dec=(heatshrink_decoder *)fh->decompData;
		heatshrink_decoder_free(dec);
//		httpd_printf("Freed %p\n", dec);
	}
#endif
//	httpd_printf("Freed %p\n", fh);
	free(fh);
}




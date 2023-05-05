#ifndef CRC32_H
#define CRC32_H

#ifdef __cplusplus 
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

	bool checkCRC32(uint8_t* buf, uint16_t size, uint32_t r);

#ifdef __cplusplus 
}
#endif

#endif  // CRC32_H

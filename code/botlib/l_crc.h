
#ifndef L_CRC_H
#define L_CRC_H

unsigned short CRC_ProcessString(unsigned char *data, int length);
void CRC_Init(unsigned short *crcvalue);
unsigned short CRC_Value(unsigned short crcvalue);
unsigned short CRC_Block(unsigned char *data, int length);

#endif

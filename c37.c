/* Program to read the binary synchrophasor data according to the IEEE c37.117
 * standard. See this for more information:
 * http://ieeexplore.ieee.org/xpl/mostRecentIssue.jsp?punumber=10719
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include "c37.h"

char *get_big_endian(char *ptr, int size, unsigned char *data) {
	int i;

    for (i = size; --i >= 0;) {
		data[i] = *ptr++;
    }
	return ptr;
}

char *put_big_endian(char *ptr, int size, unsigned char *data) {
	int i;

    for (i = size; --i >= 0;) {
		*ptr++ = data[i];
    }
	return ptr;
}

c37_packet *get_c37_packet(char *data) {
    c37_packet *pkt = malloc(sizeof(c37_packet));
	if (pkt == 0) {
		return 0;
	}

    data = get_big_endian(data, 2, (unsigned char *) &pkt->sync);
    data = get_big_endian(data, 2, (unsigned char *) &pkt->framesize);
    data = get_big_endian(data, 2, (unsigned char *) &pkt->id_code);
    data = get_big_endian(data, 4, (unsigned char *) &pkt->soc);
    data = get_big_endian(data, 4, (unsigned char *) &pkt->fracsec);
    data = get_big_endian(data, 2, (unsigned char *) &pkt->stat);
    data = get_big_endian(data, 4, (unsigned char *) &pkt->voltage_amplitude);
    data = get_big_endian(data, 4, (unsigned char *) &pkt->voltage_angle);
    data = get_big_endian(data, 4, (unsigned char *) &pkt->current_amplitude);
    data = get_big_endian(data, 4, (unsigned char *) &pkt->current_angle);
    data = get_big_endian(data, 4, (unsigned char *) &pkt->voltage_frequency);
    data = get_big_endian(data, 4, (unsigned char *) &pkt->delta_frequency);
    data = get_big_endian(data, 2, (unsigned char *) &pkt->crc);

	return pkt;
}

uint16_t ComputeCRC(unsigned char *msg, unsigned int msglen) {
    uint16_t crc = 0xFFFF;
    uint16_t temp, quick;
    unsigned int i;
    for (i=0; i<msglen; i++) {
        temp = (crc>>8) ^ msg[i];
        crc <<= 8;
        quick = temp ^ (temp >> 4);
        crc ^= quick;
        quick <<= 5;
        crc ^= quick;
        quick <<= 7;
        crc ^= quick;
    }
    return crc;
}

void form_c37_packet(char *buf, c37_packet *pkt) {
    char *ptr = buf;

    ptr = put_big_endian(ptr, 2, (unsigned char *) &pkt->sync);
    ptr = put_big_endian(ptr, 2, (unsigned char *) &pkt->framesize);
    ptr = put_big_endian(ptr, 2, (unsigned char *) &pkt->id_code);
    ptr = put_big_endian(ptr, 4, (unsigned char *) &pkt->soc);
    ptr = put_big_endian(ptr, 4, (unsigned char *) &pkt->fracsec);
    ptr = put_big_endian(ptr, 2, (unsigned char *) &pkt->stat);
    ptr = put_big_endian(ptr, 4, (unsigned char *) &pkt->voltage_amplitude);
    ptr = put_big_endian(ptr, 4, (unsigned char *) &pkt->voltage_angle);
    ptr = put_big_endian(ptr, 4, (unsigned char *) &pkt->current_amplitude);
    ptr = put_big_endian(ptr, 4, (unsigned char *) &pkt->current_angle);
    ptr = put_big_endian(ptr, 4, (unsigned char *) &pkt->voltage_frequency);
    ptr = put_big_endian(ptr, 4, (unsigned char *) &pkt->delta_frequency);
    ptr = put_big_endian(ptr, 2, (unsigned char *) &pkt->crc);
    (void) ComputeCRC((unsigned char *)buf, pkt->framesize-2);
}

void write_c37_packet(FILE *output, c37_packet *pkt) {
	char buf[FRAME_SIZE];

    form_c37_packet(buf, pkt);
	fwrite(buf, pkt->framesize, 1, output);
}

void write_c37_packet_readable(FILE *output, c37_packet *pkt) {
	time_t t = pkt->soc;
    int usec = pkt->fracsec & 0xFFFFFF;
	char *now = ctime(&t);
	fprintf(output, "%.*s:%d - %f %f %f %f\n", (int) (strlen(now) - 1), now,
		usec / 1000,
        pkt->voltage_amplitude, pkt->voltage_angle,
        pkt->current_amplitude, pkt->current_angle);
}

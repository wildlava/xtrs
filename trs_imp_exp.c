/* Copyright (c) 1996, Timothy Mann */

/* This software may be copied, modified, and used for any purpose
 * without fee, provided that (1) the above copyright notice is
 * retained, and (2) modified versions are clearly marked as having
 * been modified, with the modifier's name and the date included.  */

/* Last modified on Tue Sep 30 13:47:25 PDT 1997 by mann */

/*
 * trs_imp_exp.c
 *
 * Features to make transferring files into and out of the emulator
 *  easier.  
 */

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include "trs_imp_exp.h"
#include "z80.h"
#include "trs.h"

/* New emulator traps */

#define MAX_OPENDIR 32
DIR *dir[MAX_OPENDIR] = { NULL, };

void do_emt_open()
{
    int fd;
    fd = open(mem_pointer(REG_HL), REG_BC, REG_DE);
    if (fd >= 0) {
	REG_A = 0;
	REG_F |= ZERO_MASK;
    } else {
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
    }
    REG_DE = fd;
}

void do_emt_close()
{
    int res;
    res = close(REG_DE);
    if (res >= 0) {
	REG_A = 0;
	REG_F |= ZERO_MASK;
    } else {
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
    }
}

void do_emt_read()
{
    int size;
    if (REG_HL + REG_BC > 0x10000) {
	REG_A = EFAULT;
	REG_F &= ~ZERO_MASK;
        REG_BC = 0xFFFF;
	return;
    }
    size = read(REG_DE, mem_pointer(REG_HL), REG_BC);
    if (size >= 0) {
	REG_A = 0;
	REG_F |= ZERO_MASK;
    } else {
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
    }
    REG_BC = size;
}


void do_emt_write()
{
    int size;
    if (REG_HL + REG_BC > 0x10000) {
	REG_A = EFAULT;
	REG_F &= ~ZERO_MASK;
        REG_BC = 0xFFFF;
	return;
    }
    size = write(REG_DE, mem_pointer(REG_HL), REG_BC);
    if (size >= 0) {
	REG_A = 0;
	REG_F |= ZERO_MASK;
    } else {
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
    }
    REG_BC = size;
}

void do_emt_lseek()
{
    int i;
    off_t offset;
    if (REG_HL + 8 > 0x10000) {
	REG_A = EFAULT;
	REG_F &= ~ZERO_MASK;
	return;
    }
    offset = 0;
    for (i=REG_HL; i<8; i++) {
	offset = offset + (mem_read(REG_HL + i) << i*8);
    }
    offset = lseek(REG_DE, offset, REG_BC);
    if (offset != (off_t) -1) {
	REG_A = 0;
	REG_F |= ZERO_MASK;
    } else {
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
    }
    for (i=REG_HL; i<8; i++) {
	mem_write(REG_HL + i, offset & 0xff);
	offset >>= 8;
    }
}

void do_emt_strerror()
{
    char *msg;
    int size;
    if (REG_HL + REG_BC > 0x10000) {
	REG_A = EFAULT;
	REG_F &= ~ZERO_MASK;
	REG_BC = 0xFFFF;
	return;
    }
    errno = 0;
    msg = strerror(REG_A);
    size = strlen(msg);
    if (errno != 0) {
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
    } else if (REG_BC < size + 2) {
	REG_A = ERANGE;
	REG_F |= ZERO_MASK;
	size = REG_BC - 1;
    }
    memcpy(mem_pointer(REG_HL), msg, size);
    mem_write(REG_HL + size++, '\r');
    mem_write(REG_HL + size, '\0');
    if (errno == 0) {
	REG_BC = size;
    } else {
	REG_BC = 0xFFFF;
    }
}

void do_emt_time()
{
    time_t now = time(0);
    if (REG_A == 1) {
#if __alpha
	struct tm *loctm = localtime(&now);
        now += loctm->tm_gmtoff;
#else
	struct tm loctm = *(localtime(&now));
	struct tm gmtm = *(gmtime(&now));
        int daydiff = loctm.tm_mday - gmtm.tm_mday;
	now += (loctm.tm_sec - gmtm.tm_sec)
	     + (loctm.tm_min - gmtm.tm_min) * 60
	     + (loctm.tm_hour - gmtm.tm_hour) * 3600;
	switch (daydiff) {
	  case 0:
	  case 1:
	  case -1:
	    now += 24*3600 * daydiff;
	    break;
	  case 30:
	  case 29:
	  case 28:
          case 27:
            now -= 24*3600;
            break;
	  case -30:
	  case -29:
	  case -28:
          case -27:
            now += 24*3600;
            break;
	  default:
	    error("trouble computing local time in emt_time");
	}
#endif
    } else if (REG_A != 0) {
	error("unsupported function code to emt_time");
    }
    REG_BC = (now >> 16) & 0xffff;
    REG_DE = now & 0xffff;
}

void do_emt_opendir()
{
    int i;
    for (i = 0; i < MAX_OPENDIR; i++) {
	if (dir[i] == NULL) break;
    }
    if (i == MAX_OPENDIR) {
	REG_DE = 0xffff;
	REG_A = EMFILE;
	return;
    }
    dir[i] = opendir(mem_pointer(REG_HL));
    if (dir[i] == NULL) {
	REG_DE = 0xffff;
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
    } else {
	REG_DE = i;
	REG_A = 0;
	REG_F |= ZERO_MASK;
    }
}

void do_emt_closedir()
{
    int i = REG_DE;
    int ok;
    if (i < 0 || i >= MAX_OPENDIR || dir[i] == NULL) {
	REG_A = EBADF;
	REG_F &= ~ZERO_MASK;
	return;
    }	
    ok = closedir(dir[i]);
    dir[i] = NULL;
    if (ok >= 0) {
	REG_A = 0;
	REG_F |= ZERO_MASK;
    } else {
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
    }
}

void do_emt_readdir()
{
    int size, i = REG_DE;
    struct dirent *result;

    if (i < 0 || i >= MAX_OPENDIR || dir[i] == NULL) {
	REG_A = EBADF;
	REG_F &= ~ZERO_MASK;
	REG_BC = 0xFFFF;
	return;
    }	
    if (REG_HL + REG_BC > 0x10000) {
	REG_A = EFAULT;
	REG_F &= ~ZERO_MASK;
	REG_BC = 0xFFFF;
	return;
    }
    result = readdir(dir[i]);
    if (result == NULL) {
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
	REG_BC = 0xFFFF;
	return;
    }
    size = strlen(result->d_name);
    if (size + 1 > REG_BC) {
	REG_A = ERANGE;
	REG_F &= ~ZERO_MASK;
	REG_BC = 0xFFFF;
	return;
    }
    strcpy(mem_pointer(REG_HL), result->d_name);
    REG_A = 0;
    REG_F |= ZERO_MASK;
    REG_BC = size;
}

void do_emt_chdir()
{
    int ok = chdir(mem_pointer(REG_HL));
    if (ok < 0) {
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
    } else {
	REG_A = 0;
	REG_F |= ZERO_MASK;
    }
}

void do_emt_getcwd()
{
    char *result;
    if (REG_HL + REG_BC > 0x10000) {
	REG_A = EFAULT;
	REG_F &= ~ZERO_MASK;
	REG_BC = 0xFFFF;
	return;
    }
    result = getcwd(mem_pointer(REG_HL), REG_BC);
    if (result == NULL) {
	REG_A = errno;
	REG_F &= ~ZERO_MASK;
	REG_BC = 0xFFFF;
	return;
    }
    REG_A = 0;
    REG_F |= ZERO_MASK;
    REG_BC = strlen(result);
}

void do_emt_misc()
{
    switch (REG_A) {
      case 0:
	trs_disk_change_all();
	break;
      case 1:
	trs_exit();
	break;
      case 2:
	trs_debug();
	break;
      case 3:
	trs_reset();
	break;
      default:
	error("unsupported function code to emt_misc");
	break;
    }
}

/* Old kludgey way using i/o ports */

typedef struct {
  FILE* f;
  unsigned char cmd;
  unsigned char status;
  char filename[IMPEXP_MAX_CMD_LEN+1];
  int len;
} Channel;

Channel ch;

static void
reset()
{
    if (ch.f) {
	int v;
	v = fclose(ch.f);
	ch.f = NULL;
	if (v == 0) {
	    ch.status = IMPEXP_EOF;
	} else {
	    ch.status = errno;
	    if (ch.status == 0) ch.status = IMPEXP_UNKNOWN_ERROR;
	}
    } else {
	ch.status = IMPEXP_EOF;
    }
    ch.cmd = IMPEXP_CMD_RESET;
    ch.len = 0;
}

/* Common routine for writing file names */
static void
filename_write(char* dir, unsigned char data)
{
    if (ch.len < IMPEXP_MAX_CMD_LEN) {
	ch.filename[ch.len++] = data;
    }
    if (data == 0) {
	ch.f = fopen(ch.filename, dir);
	if (ch.f == NULL) {
	    ch.status = errno;
	    if (ch.status == 0) {
		/* Bogus popen, doesn't set errno */
		ch.status = IMPEXP_UNKNOWN_ERROR;
	    }
	    ch.cmd = IMPEXP_CMD_RESET;
	} else {
	    ch.status = IMPEXP_EOF;
	}
    }
}

void
trs_impexp_cmd_write(unsigned char data)
{
    switch (data) {
      case IMPEXP_CMD_RESET:
      case IMPEXP_CMD_EOF:
	reset();
	break;
      case IMPEXP_CMD_IMPORT:
      case IMPEXP_CMD_EXPORT:
	if (ch.cmd != IMPEXP_CMD_RESET) reset();
	ch.cmd = data;
	break;
    }
}

unsigned char
trs_impexp_status_read()
{
    return ch.status;
}

void
trs_impexp_data_write(unsigned char data)
{
    int c;
    switch (ch.cmd) {
      case IMPEXP_CMD_RESET:
      case IMPEXP_CMD_EOF:
	/* ignore */
	break;
      case IMPEXP_CMD_IMPORT:
	if (ch.f != NULL) {
	    /* error; ignore */
	} else {
	    filename_write("r", data);
	}
	break;
      case IMPEXP_CMD_EXPORT:
	if (ch.f != NULL) {
	    c = putc(data, ch.f);
	    if (c == EOF) {
		reset();
	    }
	} else {
	    filename_write("w", data);
	}
	break;
    }	
}

unsigned char
trs_impexp_data_read()
{
    switch (ch.cmd) {
      case IMPEXP_CMD_RESET:
      case IMPEXP_CMD_EOF:
	break;
      case IMPEXP_CMD_IMPORT:
	if (ch.f != NULL) {
	    int c = getc(ch.f);
	    if (c == EOF) {
		reset();
	    } else {
		ch.status = IMPEXP_MORE_DATA;
		return c;
	    }
	}
	break;
      case IMPEXP_CMD_EXPORT:
	break;
    }	
    /* Return end of file or error */
    if (ch.status == 0) ch.status = IMPEXP_EOF;
    return IMPEXP_EOF;
}

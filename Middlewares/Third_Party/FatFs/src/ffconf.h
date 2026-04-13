/*---------------------------------------------------------------------------/
/  FatFs Configurations for H562 Loadcell Datalogger
/  Based on FatFs R0.15 (ffconf.h template)
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	80286	/* Revision ID — must match ff.h */

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY	0
#define FF_FS_MINIMIZE	0
#define FF_USE_FIND		1
#define FF_USE_MKFS		1	/* Allow formatting from firmware */
#define FF_USE_FASTSEEK	1	/* Pre-allocation for logging */
#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	1
#define FF_USE_FORWARD	0

#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	0

/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

#define FF_CODE_PAGE	437	/* US English */

#define FF_USE_LFN		1	/* Long file names, static working buffer */
#define FF_MAX_LFN		255

#define FF_LFN_UNICODE	0	/* OEM char set (TCHAR = char) */
#define FF_LFN_BUF		255
#define FF_SFN_BUF		12

#define FF_FS_RPATH		0

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/

#define FF_VOLUMES		2
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"SD"

#define FF_MULTI_PARTITION	1

#define FF_MIN_SS		512
#define FF_MAX_SS		512

#define FF_LBA64		0
#define FF_MIN_GPT		0x10000000
#define FF_USE_TRIM		0

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_TINY		0	/* Full buffering (not tiny) */

#define FF_FS_EXFAT		0

#define FF_FS_NORTC		0	/* Use RTC for timestamps */
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2026

#define FF_FS_NOFSINFO	0

#define FF_FS_LOCK		0

#define FF_FS_REENTRANT	0	/* No RTOS, no reentrancy needed */
#define FF_FS_TIMEOUT	1000

/*--- End of configuration options ---*/
